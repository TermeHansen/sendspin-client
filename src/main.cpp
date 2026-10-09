// Copyright 2026 TermeHansen
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

/// @file Host example application for sendspin-cpp.
///
/// Runs a SendspinClient on the host computer, listening for incoming
/// connections from a Sendspin server on port 8928. Advertises via mDNS
/// so Sendspin servers can discover and connect automatically.
///
/// Usage: ./basic_client [options] [name]
///   name:  Optional friendly name (default: "Basic Client")
///
/// Options:
///   -u URL        Connect to a WebSocket URL (e.g. ws://192.168.1.10:8928/sendspin)
///   -p PORT       Port for the local WebSocket server (default: 8928)
///   -l LEVEL      Set log level: none, error, warn, info (default), debug, verbose
///   -v            Verbose logging (same as -l verbose)
///   -q            Quiet logging (same as -l error)
///   -L            List available audio devices and exit
///   -d DEVICE     Select audio device by index (use -L to list devices)
///   -m MIXER      Use ALSA hardware mixer for volume control (format: card:control, e.g., "1:Digital")
///   -c FILE       Use configuration file (default: /etc/sendspin-client/sendspin-client.conf)
///   -t SECONDS    Idle timeout in seconds before releasing audio device (0 = disable, default)
///   -a            Allow unpaired servers to play (default: off; they must pair first)
///   -h            Show usage

#include "sendspin/client.h"
#include "sendspin/metadata_role.h"
#include "sendspin/player_role.h"
#ifdef SENDSPIN_HAS_PORTAUDIO
#include "portaudio_sink.h"
#include <portaudio.h>
#endif

#include <dns_sd.h>
#include <getopt.h>

#include <arpa/inet.h>

#include <algorithm>
#include <atomic>
#include "config_parser.h"
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <optional>
#include <string>
#include <sys/stat.h>
#include <unistd.h>  // for access()
#include <thread>
#include <vector>
#include "utils.h"

using namespace sendspin;

static const uint16_t SENDSPIN_PORT = 8928;
static const char* SENDSPIN_PATH = "/sendspin";
static uint16_t server_port = SENDSPIN_PORT;

// Tracks total audio bytes received (used when PortAudio is unavailable)
static size_t null_audio_total_bytes = 0;

// Manages mDNS service advertisement via dns_sd.h
class MdnsAdvertiser {
public:
    ~MdnsAdvertiser() {
        stop();
    }

    bool start(const std::string& name, uint16_t port, const std::string& path) {
        // Build TXT record with path and name keys
        TXTRecordRef txt;
        TXTRecordCreate(&txt, 0, nullptr);
        TXTRecordSetValue(&txt, "path", static_cast<uint8_t>(path.size()), path.c_str());
        TXTRecordSetValue(&txt, "name", static_cast<uint8_t>(name.size()), name.c_str());

        DNSServiceErrorType err = DNSServiceRegister(
            &service_ref_,
            0,                    // flags
            0,                    // interface index (0 = all)
            name.c_str(),         // service name
            "_sendspin._tcp",     // service type
            nullptr,              // domain (default)
            nullptr,              // host (default)
            htons(port),          // port (network byte order)
            TXTRecordGetLength(&txt),
            TXTRecordGetBytesPtr(&txt),
            nullptr,              // callback (not needed for simple registration)
            nullptr               // context
        );

        TXTRecordDeallocate(&txt);

        if (err != kDNSServiceErr_NoError) {
            fprintf(stderr, "Failed to register mDNS service: error %d\n", err);
            return false;
        }

        fprintf(stderr, "mDNS: Advertising _sendspin._tcp on port %u (name: %s)\n", port,
                name.c_str());
        return true;
    }

    void stop() {
        if (service_ref_ != nullptr) {
            DNSServiceRefDeallocate(service_ref_);
            service_ref_ = nullptr;
            fprintf(stderr, "mDNS: Service advertisement stopped\n");
        }
    }

private:
    DNSServiceRef service_ref_{nullptr};
};

// File-backed SendspinPersistenceProvider.
//
// sendspin-cpp v0.9.0 derives the client identity from a static X25519 keypair and stores
// pairing records through an opaque byte store: load_blob()/save_blob()/commit() keyed by the
// fixed keys in sendspin/persistence_keys.h. The library owns all serialization; this provider
// only stores bytes, one file per key. Without it the keypair is regenerated every boot, so
// pairing and server preference would not survive a restart.
//
// Every call arrives on the main loop thread, so no locking is needed here.
class HostPersistenceProvider : public SendspinPersistenceProvider {
public:
    explicit HostPersistenceProvider(std::string dir) : dir_(std::move(dir)) {}

    std::optional<std::vector<uint8_t>> load_blob(const std::string& key) override {
        std::ifstream in(path_for(key), std::ios::binary);
        if (!in) {
            return std::nullopt;
        }
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                   std::istreambuf_iterator<char>());
        if (!in.good() && !in.eof()) {
            return std::nullopt;
        }
        return bytes;
    }

    bool save_blob(const std::string& key, const uint8_t* data, size_t len) override {
        // Keys are library-owned identifiers ([a-z0-9_]); reject anything path-like defensively.
        if (key.empty() || key.find('/') != std::string::npos || key.find("..") != std::string::npos) {
            return false;
        }
        if (!ensure_dir()) {
            this->note_failure();
            return false;
        }
        const std::string final_path = path_for(key);
        const std::string tmp_path = final_path + ".tmp";
        {
            // Write to a private temp file and rename, so a crash mid-write cannot leave a
            // truncated record (a half-written KEYPAIR or record slot would corrupt the stored
            // identity). The file holds the X25519 private key and pairing PSKs, so it is created
            // 0600 regardless of the directory's permissions and the process umask; the rename
            // preserves that mode.
            const int fd = ::open(tmp_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
            if (fd < 0) {
                this->note_failure();
                return false;
            }
            size_t written = 0;
            while (written < len) {
                const ssize_t n = ::write(fd, data + written, len - written);
                if (n < 0) {
                    if (errno == EINTR) {
                        continue;
                    }
                    break;
                }
                written += static_cast<size_t>(n);
            }
            // Flush the bytes to the platter before the rename publishes them: a power loss after
            // a successful save must not lose the identity or a pairing record.
            const bool ok = written == len && ::fsync(fd) == 0;
            ::close(fd);
            if (!ok) {
                ::unlink(tmp_path.c_str());
                this->note_failure();
                return false;
            }
        }
        if (::rename(tmp_path.c_str(), final_path.c_str()) != 0) {
            ::unlink(tmp_path.c_str());
            this->note_failure();
            return false;
        }
        // The rename is visible in the directory only after the directory entry is durable.
        this->sync_dir();
        return true;
    }

    // Writes are durable when save_blob() returns, so nothing is pending here.
    bool commit() override { return true; }

    const std::string& dir() const { return dir_; }

    /// @brief True if any save_blob() call has failed. The caller checks this after start() to
    /// warn when the identity could not be persisted, rather than probing the directory with
    /// access(), which cannot tell an unwritable directory from a failed write.
    bool save_failed() const { return save_failed_; }

private:
    std::string path_for(const std::string& key) const { return dir_ + "/" + key; }

    void note_failure() { this->save_failed_ = true; }

    void sync_dir() {
        const int dfd = ::open(dir_.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (dfd >= 0) {
            ::fsync(dfd);
            ::close(dfd);
        }
    }

    bool ensure_dir() {
        if (::mkdir(dir_.c_str(), 0700) == 0) {
            return true;
        }
        if (errno != EEXIST) {
            return false;
        }
        // An existing path is fine only if it really is a directory; a regular file or symlink
        // there would make every path_for() write fail in a confusing way.
        struct stat st{};
        return ::stat(dir_.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
    }

    std::string dir_;
    bool save_failed_{false};
};

static std::atomic<bool> running{true};

static void signal_handler(int /*sig*/) {
    running.store(false);
}

static void print_usage(const char* prog) {
    fprintf(stderr, "Usage: %s [options] [name]\n", prog);
    fprintf(stderr, "  name          Friendly name (default: \"Basic Client\")\n\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -u URL        Connect to a WebSocket URL (e.g. ws://192.168.1.10:8928/sendspin)\n");
    fprintf(stderr, "  -p PORT       Port for the local WebSocket server (default: 8928)\n");
    fprintf(stderr, "  -l LEVEL      Log level: none, error, warn, info (default), debug, verbose\n");
    fprintf(stderr, "  -v            Verbose logging (same as -l verbose)\n");
    fprintf(stderr, "  -q            Quiet logging (same as -l error)\n");
    fprintf(stderr, "  -L            List available audio devices and exit\n");
    fprintf(stderr, "  -d DEVICE     Select audio device by index (use -L to list devices)\n");
    fprintf(stderr, "  -m MIXER      Use ALSA hardware mixer for volume control (format: card:control, e.g., \"1:Digital\")\n");
    fprintf(stderr, "  -c FILE       Use configuration file (default: /etc/sendspin-client/sendspin-client.conf)\n");
    fprintf(stderr, "  -t SECONDS    Idle timeout in seconds before releasing audio device (0 = disable, default)\n");
    fprintf(stderr, "  -a            Allow unpaired servers to play (default: off; they must pair first)\n");
    fprintf(stderr, "  -h            Show this help\n");
}

static bool parse_log_level(const char* str, LogLevel& level) {
    if (strcmp(str, "none") == 0) { level = LogLevel::NONE; return true; }
    if (strcmp(str, "error") == 0) { level = LogLevel::ERROR; return true; }
    if (strcmp(str, "warn") == 0) { level = LogLevel::WARN; return true; }
    if (strcmp(str, "info") == 0) { level = LogLevel::INFO; return true; }
    if (strcmp(str, "debug") == 0) { level = LogLevel::DEBUG; return true; }
    if (strcmp(str, "verbose") == 0) { level = LogLevel::VERBOSE; return true; }
    return false;
}

// Reconnect policy for outbound connections (-u / CONNECT_URL).
//
// The sendspin-cpp liveness watchdog drops a connection whose inbound silence exceeds the
// timeout, but upstream leaves reconnection to the consumer (sendspin-cpp v0.8.0): only this
// client knows whether it drives an outbound connection and which policy fits. This ports the
// behavior of the local sendspin-cpp patch shipped with v0.2.1: the backoff arms only after a
// connection has been established, the first retry is immediate, the delay doubles per attempt
// up to RETRY_DELAY_MAX_S, and everything resets once the target answers again. Inbound
// (discovery) connections are never reconnected here; their server is expected to reconnect.
struct OutboundReconnect {
    bool enabled{false};
    std::string url;
    bool was_connected{false};
    std::chrono::steady_clock::time_point next_attempt{std::chrono::steady_clock::now()};
    uint32_t delay_s{1};

    static constexpr uint32_t RETRY_DELAY_MAX_S = 30;

    void tick(SendspinClient& client) {
        if (!enabled) {
            return;
        }
        if (client.is_connected()) {
            this->was_connected = true;
            this->delay_s = 1;
            // Clear any pending retry schedule from earlier failed attempts (the superseded
            // library patch erased its reconnect entry on promotion). Without this, a drop
            // right after a recovery would wait out the stale backoff instead of retrying
            // immediately.
            this->next_attempt = std::chrono::steady_clock::now();
            return;
        }
        if (!this->was_connected || std::chrono::steady_clock::now() < this->next_attempt) {
            return;
        }
        fprintf(stderr, ">>> Connection lost, reconnecting to %s\n", this->url.c_str());
        client.connect_to(this->url);
        // Double the backoff before scheduling the next attempt (first retry is immediate, the
        // next waits out the doubled delay), capped. Scheduling now — not after the connect
        // resolves — bounds the attempt rate for a target that fails silently.
        this->delay_s = std::min<uint32_t>(this->delay_s * 2, RETRY_DELAY_MAX_S);
        this->next_attempt = std::chrono::steady_clock::now() +
                             std::chrono::seconds(this->delay_s);
    }
};

int main(int argc, char* argv[]) {
    // Set up signal handler for clean shutdown
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // Parse command line options
    LogLevel log_level = LogLevel::INFO;
    std::string connect_url;
    int audio_device_index = -1;
    bool list_devices = false;
    std::string alsa_mixer_spec;
    std::string config_file;
    int idle_timeout_s = 0;
    int server_port_arg = -1;
    int liveness_timeout_s = -1;  // -1 = library default (derived from burst settings, 60 s)
    bool reconnect_on_loss = true;  // Reconnect an outbound (-u) connection after liveness loss
    int enable_mdns_cli = -1;  // -1 = unset; config file or default decides
    int unpaired_access_cli = -1;  // -1 = unset; config file or default (off) decides
    int opt;
    while ((opt = getopt(argc, argv, "u:p:l:vqhd:m:Lc:t:a")) != -1) {
        switch (opt) {
            case 'u':
                connect_url = optarg;
                break;
            case 'p':
                server_port_arg = std::atoi(optarg);
                break;
            case 'l':
                if (!parse_log_level(optarg, log_level)) {
                    fprintf(stderr, "Unknown log level: %s\n", optarg);
                    print_usage(argv[0]);
                    return 1;
                }
                break;
            case 'v':
                log_level = LogLevel::VERBOSE;
                break;
            case 'q':
                log_level = LogLevel::ERROR;
                break;
            case 'd':
                audio_device_index = std::atoi(optarg);
                break;
            case 'm':
                alsa_mixer_spec = optarg;
                break;
            case 'c':
                config_file = optarg;
                break;
            case 'L':
                list_devices = true;
                break;
            case 't':
                idle_timeout_s = std::atoi(optarg);
                break;
            case 'a':
                unpaired_access_cli = 1;
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    SendspinClient::set_log_level(log_level);

    // Load configuration from file if specified
    ConfigParser config_parser;
    std::string effective_config_file = config_file;
    
    // Use default config file if none specified
    if (effective_config_file.empty()) {
        effective_config_file = "/etc/sendspin-client/sendspin-client.conf";
    }
    
    // Try to load the configuration file
    if (!config_file.empty() || access(effective_config_file.c_str(), R_OK) == 0) {
        if (config_parser.parse(effective_config_file)) {
            // Override command-line options with config file values if not specified
            if (connect_url.empty() && config_parser.has_key("CONNECT_URL")) {
                connect_url = config_parser.get_string("CONNECT_URL");
            }
            
            if (audio_device_index == -1 && config_parser.has_key("AUDIO_DEVICE")) {
                audio_device_index = config_parser.get_int("AUDIO_DEVICE", -1);
            }
            
            if (alsa_mixer_spec.empty() && config_parser.has_key("ALSA_MIXER_SPEC")) {
                alsa_mixer_spec = config_parser.get_string("ALSA_MIXER_SPEC");
            }

            if (idle_timeout_s == 0 && config_parser.has_key("IDLE_TIMEOUT")) {
                idle_timeout_s = config_parser.get_int("IDLE_TIMEOUT", 0);
            }

            if (config_parser.has_key("LIVENESS_TIMEOUT")) {
                liveness_timeout_s = config_parser.get_int("LIVENESS_TIMEOUT", 60);
            }

            if (config_parser.has_key("RECONNECT_ON_LOSS")) {
                reconnect_on_loss = config_parser.get_bool("RECONNECT_ON_LOSS", true);
            }

            if (server_port_arg == -1 && config_parser.has_key("SERVER_PORT")) {
                server_port_arg = config_parser.get_int("SERVER_PORT", SENDSPIN_PORT);
            }

            if (enable_mdns_cli == -1 && config_parser.has_key("ENABLE_MDNS")) {
                enable_mdns_cli = config_parser.get_bool("ENABLE_MDNS", true) ? 1 : 0;
            }

            if (unpaired_access_cli == -1 && config_parser.has_key("UNPAIRED_ACCESS")) {
                unpaired_access_cli = config_parser.get_bool("UNPAIRED_ACCESS", false) ? 1 : 0;
            }

            // Set log level from config if not specified on command line
            if (optind == 1) {  // No command-line options were specified
                std::string log_level_str = config_parser.get_string("LOG_LEVEL", "info");
                if (!parse_log_level(log_level_str.c_str(), log_level)) {
                    fprintf(stderr, "Warning: Unknown log level in config: %s\n", log_level_str.c_str());
                }
                SendspinClient::set_log_level(log_level);
            }
        } else {
            fprintf(stderr, "Warning: Could not read configuration file %s\n", effective_config_file.c_str());
        }
    }

    // Get friendly name from config file if available
    std::string friendly_name = "Basic Client";
    if (config_parser.has_key("FRIENDLY_NAME")) {
        friendly_name = config_parser.get_string("FRIENDLY_NAME", "Basic Client");
    } else if (optind < argc) {
        // Use command-line argument if provided
        friendly_name = argv[optind];
    }

    // Resolve the server port before the startup banner so the printed value matches what the
    // client and the mDNS advertisement actually use.
    if (server_port_arg > 0) {
        server_port = static_cast<uint16_t>(server_port_arg);
    }

    // --- STARTUP LOGGING BLOCK ---
    fprintf(stderr, "\n");
    fprintf(stderr, "=================================================================\n");
    fprintf(stderr, " Sendspin Client - Version: %s\n", PROJECT_VERSION);
    fprintf(stderr, " Build Date: %s %s\n", __DATE__, __TIME__);
    fprintf(stderr, "=================================================================\n");
    fprintf(stderr, " Active Configuration:\n");
    fprintf(stderr, "  Config File   : %s\n", effective_config_file.c_str());
    fprintf(stderr, "  Friendly Name : %s\n", friendly_name.c_str());
    fprintf(stderr, "  Audio Device  : %d\n", audio_device_index);
    fprintf(stderr, "  ALSA Mixer    : %s\n", alsa_mixer_spec.empty() ? "None" : alsa_mixer_spec.c_str());
    fprintf(stderr, "  Connect URL   : %s\n", connect_url.empty() ? "Listen Mode (Server)" : connect_url.c_str());
    fprintf(stderr, "  Idle Timeout  : %s\n", idle_timeout_s == 0 ? "inaktiv" : (std::to_string(idle_timeout_s) + " seconds").c_str());
    fprintf(stderr, "  Liveness      : %s\n", liveness_timeout_s < 0 ? "default (60 s)" : (liveness_timeout_s == 0 ? "disabled" : (std::to_string(liveness_timeout_s) + " seconds").c_str()));
    fprintf(stderr, "  Reconnect     : %s\n", reconnect_on_loss ? "on" : "off");
    fprintf(stderr, "  Server Port   : %u\n", server_port);
    fprintf(stderr, "  mDNS          : %s\n", enable_mdns_cli == 0 ? "off" : "on");
    fprintf(stderr, "  Unpaired      : %s\n", unpaired_access_cli == 1 ? "allowed" : "off (pairing required)");
    const char* log_level_str = "info";
    switch (log_level) {
        case LogLevel::NONE: log_level_str = "none"; break;
        case LogLevel::ERROR: log_level_str = "error"; break;
        case LogLevel::WARN: log_level_str = "warn"; break;
        case LogLevel::INFO: log_level_str = "info"; break;
        case LogLevel::DEBUG: log_level_str = "debug"; break;
        case LogLevel::VERBOSE: log_level_str = "verbose"; break;
    }
    fprintf(stderr, "  Log Level     : %s\n", log_level_str);
    fprintf(stderr, "=================================================================\n\n");
    // --- END STARTUP LOGGING BLOCK ---

    // Handle device listing request
#ifdef SENDSPIN_HAS_PORTAUDIO
    if (list_devices) {
        // Initialize PortAudio just to list devices
        PaError err = Pa_Initialize();
        if (err == paNoError) {
            PortAudioSink::list_devices();
            Pa_Terminate();
        } else {
            fprintf(stderr, "PortAudio init failed: %s\n", Pa_GetErrorText(err));
        }
        return 0;
    }
#endif

    // Configure the client. The client identity is no longer a configured string: sendspin-cpp
    // v0.9.0 derives it from a static X25519 keypair (persisted below) and exposes it via
    // client.client_id() once start() has run.
    SendspinClientConfig config;
    config.name = friendly_name;
    config.product_name = "sendspin-client";
    config.manufacturer = "sendspin-cpp";
    config.software_version = PROJECT_VERSION;
    config.server_port = server_port;

    if (liveness_timeout_s >= 0) {
        // Liveness watchdog: drop an established connection after this many seconds of inbound
        // silence (0 disables). The library default is derived from the burst settings (60 s).
        config.liveness_timeout_ms = static_cast<int64_t>(liveness_timeout_s) * 1000;
    }

    // Create audio output and client
#ifdef SENDSPIN_HAS_PORTAUDIO
    PortAudioSink audio_sink;
    if (!alsa_mixer_spec.empty()) {
        audio_sink.set_alsa_mixer_spec(alsa_mixer_spec);
        
        // Try to read and display current hardware volume
        int current_volume = PortAudioSink::get_alsa_volume(alsa_mixer_spec);
        if (current_volume >= 0) {
            fprintf(stderr, "ALSA mixer '%s' current volume: %d%%\n", alsa_mixer_spec.c_str(), current_volume);
        } else {
            fprintf(stderr, "Warning: Could not read current volume from ALSA mixer '%s'\n", alsa_mixer_spec.c_str());
        }
    }
#endif

    SendspinClient client(std::move(config));

    // Add roles
    PlayerRoleConfig player_config;

#ifdef SENDSPIN_HAS_PORTAUDIO
    // Dynamically determine supported audio formats based on device capabilities
    int device_to_check = audio_device_index >= 0 ? audio_device_index : Pa_GetDefaultOutputDevice();

    fprintf(stderr, "Checking supported formats for device %d...\n", device_to_check);

    // Test common sample rates
    std::vector<uint32_t> sample_rates = {44100, 48000, 96000, 192000};
    std::vector<uint8_t> bit_depths = {16, 24, 32};

    for (uint32_t sample_rate : sample_rates) {
        for (uint8_t bit_depth : bit_depths) {
            if (PortAudioSink::is_format_supported(device_to_check, sample_rate, 2, bit_depth)) {
                fprintf(stderr, "  Device supports: %uHz 2ch %ubit\n", sample_rate, bit_depth);

                // Add FLAC format if supported
                player_config.audio_formats.push_back({SendspinCodecFormat::FLAC, 2, sample_rate, bit_depth});

                // Add PCM format if supported
                player_config.audio_formats.push_back({SendspinCodecFormat::PCM, 2, sample_rate, bit_depth});

                // Add OPUS format for common sample rates (OPUS typically uses 48kHz)
                if (sample_rate == 48000) {
                    player_config.audio_formats.push_back({SendspinCodecFormat::OPUS, 2, sample_rate, 16});
                }
            }
        }
    }
#endif

    if (player_config.audio_formats.empty()) {
        fprintf(stderr, "Warning: No supported formats found for device %d, using defaults\n", device_to_check);
        player_config.audio_formats = {
            {SendspinCodecFormat::FLAC, 2, 44100, 16},
            {SendspinCodecFormat::FLAC, 2, 48000, 16},
            {SendspinCodecFormat::OPUS, 2, 48000, 16},
            {SendspinCodecFormat::PCM, 2, 44100, 16},
            {SendspinCodecFormat::PCM, 2, 48000, 16},
        };
    }
    auto& player = client.add_player(std::move(player_config));
    
    // Set initial volume and mute to match hardware mixer if using ALSA control
    if (!alsa_mixer_spec.empty()) {
        int current_volume = PortAudioSink::get_alsa_volume(alsa_mixer_spec);
        if (current_volume >= 0) {
            player.update_volume(static_cast<uint8_t>(current_volume));
        }
        
        bool current_mute = PortAudioSink::get_alsa_mute(alsa_mixer_spec);
        player.update_muted(current_mute);
    }
    
    auto& metadata = client.add_metadata();

    // --- Listener implementations ---

    struct BasicPlayerListener : PlayerRoleListener {
        bool stream_active{false};
        bool device_open{false};
        std::chrono::steady_clock::time_point last_stream_end_time{std::chrono::steady_clock::now()};
        int idle_timeout_s{0};

#ifdef SENDSPIN_HAS_PORTAUDIO
        PortAudioSink& sink;
        PlayerRole& player;
        int audio_device_index;
        BasicPlayerListener(PortAudioSink& s, PlayerRole& p, int device_index, int timeout_s) 
            : idle_timeout_s(timeout_s), sink(s), player(p), audio_device_index(device_index) {}
#else
        BasicPlayerListener(int timeout_s) : idle_timeout_s(timeout_s) {}
#endif

        size_t on_audio_write(uint8_t* data, size_t length, uint32_t timeout_ms) override {
#ifdef SENDSPIN_HAS_PORTAUDIO
            return sink.write(data, length, timeout_ms);
#else
            (void)data;
            (void)timeout_ms;
            null_audio_total_bytes += length;
            return length;
#endif
        }

        void on_stream_start() override {
            fprintf(stderr, ">>> Stream started\n");
            this->stream_active = true;
            this->device_open = true;
#ifdef SENDSPIN_HAS_PORTAUDIO
            auto& params = player.get_current_stream_params();
            if (params.sample_rate.has_value() && params.channels.has_value() &&
                params.bit_depth.has_value()) {
                sink.configure(*params.sample_rate, *params.channels, *params.bit_depth, audio_device_index);
            } else {
                fprintf(stderr, ">>> Stream params not yet available for PortAudio\n");
            }
#endif
        }

        void on_stream_end() override {
            fprintf(stderr, ">>> Stream ended\n");
            this->stream_active = false;
            this->last_stream_end_time = std::chrono::steady_clock::now();
            
            if (this->idle_timeout_s > 0) {
                fprintf(stderr, ">>> Audio device will be released in %d seconds if idle\n", this->idle_timeout_s);
            }

#ifdef SENDSPIN_HAS_PORTAUDIO
            sink.clear();
#endif
        }

#ifdef SENDSPIN_HAS_PORTAUDIO
        void on_volume_changed(uint8_t vol) override { sink.set_volume(vol); }
        void on_mute_changed(bool muted) override { sink.set_muted(muted); }
#endif
    };

    struct BasicMetadataListener : MetadataRoleListener {
        void on_metadata(const ServerMetadataStateObject& md) override {
            if (md.title.has_value()) {
                fprintf(stderr, ">>> Metadata: %s - %s\n",
                        md.artist.value_or("Unknown").c_str(), md.title->c_str());
            }
        }
    };

    struct BasicClientListener : SendspinClientListener {
        void on_time_sync_updated(float error) override {
            if (SendspinClient::get_log_level() >= LogLevel::DEBUG) {
                fprintf(stderr, ">>> Time sync error: %.1f us\n", error);
            }
        }
    };

    struct HostNetworkProvider : SendspinNetworkProvider {
        bool is_network_ready() override { return true; }
    };

#ifdef SENDSPIN_HAS_PORTAUDIO
    BasicPlayerListener player_listener(audio_sink, player, audio_device_index, idle_timeout_s);
    audio_sink.on_frames_played = [&player](uint32_t frames, int64_t timestamp) {
        player.notify_audio_played(frames, timestamp);
    };
#else
    BasicPlayerListener player_listener(idle_timeout_s);
#endif
    BasicMetadataListener metadata_listener;
    BasicClientListener client_listener;
    HostNetworkProvider network_provider;
    // Persists the X25519 identity, Pairing PSK and pairing records across restarts. Set before
    // start(), which loads (or generates) the identity and writes it through this provider.
    // SENDSPIN_STATE_DIR overrides the path (useful for unprivileged/dev runs); the service user
    // owns the default directory.
    const char* state_dir_env = getenv("SENDSPIN_STATE_DIR");
    HostPersistenceProvider persistence_provider(
        state_dir_env != nullptr && state_dir_env[0] != '\0' ? state_dir_env : "/var/lib/sendspin-client");

    player.set_listener(&player_listener);
    metadata.set_listener(&metadata_listener);
    client.set_listener(&client_listener);
    client.set_network_provider(&network_provider);
    client.set_persistence_provider(&persistence_provider);

    // Unpaired access: with the v0.9 Noise protocol a server must pair before it may play,
    // unless this is enabled. Off by default; restored before start() so the first client/hello
    // already advertises it. The library never persists this setting itself.
    client.set_unpaired_access_enabled(unpaired_access_cli == 1);

    // Start the server
    fprintf(stderr, "Starting Sendspin basic client on port %u...\n", server_port);

    if (!client.start()) {
        fprintf(stderr, "Failed to start server\n");
        return 1;
    }

    // The identity is derived from the persisted keypair and only readable after start().
    fprintf(stderr, "Client ID: %s\n", client.client_id().c_str());
    if (persistence_provider.save_failed()) {
        fprintf(stderr,
                "Warning: identity could not be persisted to %s; it will change on restart and "
                "requires pairing again\n",
                persistence_provider.dir().c_str());
    }

    // Advertise via mDNS
    MdnsAdvertiser mdns;
    bool mdns_started = false;
    if (enable_mdns_cli != 0) {
        mdns_started = mdns.start(friendly_name, server_port, SENDSPIN_PATH);
        if (!mdns_started) {
            fprintf(stderr, "Warning: mDNS advertisement failed, server still running\n");
            fprintf(stderr, "Connect manually to ws://<this-host>:%u%s\n", server_port,
                    SENDSPIN_PATH);
        }
    }

    // Auto-connect if a URL was provided via -u
    OutboundReconnect reconnect;
    if (!connect_url.empty()) {
        fprintf(stderr, "Connecting to %s...\n", connect_url.c_str());
        client.connect_to(connect_url);
        reconnect.enabled = reconnect_on_loss;
        reconnect.url = connect_url;
    }

    fprintf(stderr, "Press Ctrl+C to stop.\n\n");

    // Main loop
    int tick = 0;
    uint8_t last_volume = player.get_volume();
    bool last_muted = player.get_muted();
    while (running.load()) {
        client.loop();
        reconnect.tick(client);
#ifdef SENDSPIN_HAS_PORTAUDIO
        // Sync audio sink volume periodically (catches all volume change sources)
        if (++tick % 25 == 0) {
            uint8_t current_volume = player.get_volume();
            bool current_muted = player.get_muted();
            
            // Only update if values have changed
            if (current_volume != last_volume) {
                audio_sink.set_volume(current_volume);
                last_volume = current_volume;
            }
            if (current_muted != last_muted) {
                audio_sink.set_muted(current_muted);
                last_muted = current_muted;
            }
        }

        // Idle timeout check to release audio device
        if (idle_timeout_s > 0 && !player_listener.stream_active && player_listener.device_open) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - player_listener.last_stream_end_time).count();
            if (elapsed >= idle_timeout_s) {
                fprintf(stderr, ">>> Idle timeout reached (%d s), releasing audio device\n", idle_timeout_s);
                audio_sink.stop();
                player_listener.device_open = false;
            }
        }
#else
        ++tick;
#endif
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    fprintf(stderr, "\nShutting down...\n");
    if (mdns_started) {
        mdns.stop();
    }
    client.stop();

#ifndef SENDSPIN_HAS_PORTAUDIO
    fprintf(stderr, "Total audio bytes received: %zu\n", null_audio_total_bytes);
#endif
    return 0;
}
