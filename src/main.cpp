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
///   -u URL    Connect to a WebSocket URL (e.g. ws://192.168.1.10:8928/sendspin)
///   -l LEVEL  Set log level: none, error, warn, info (default), debug, verbose
///   -v        Verbose logging (same as -l verbose)
///   -q        Quiet logging (same as -l error)
///   -L        List available audio devices and exit
///   -d DEVICE Select audio device by index (use -L to list devices)
///   -m MIXER  Use ALSA hardware mixer for volume control (format: card:control, e.g., "1:Digital")
///   -h        Show usage

#include "sendspin/client.h"
#include "sendspin/controller_role.h"
#include "sendspin/metadata_role.h"
#include "sendspin/player_role.h"
#include "sendspin/player_role.h"
#ifdef SENDSPIN_HAS_PORTAUDIO
#include "portaudio_sink.h"
#include <portaudio.h>
#endif

#include <dns_sd.h>
#include <getopt.h>

#include <arpa/inet.h>

#include <atomic>
#include "config_parser.h"
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>  // for access()
#include <thread>
#include "utils.h"

using namespace sendspin;

static const uint16_t SENDSPIN_PORT = 8928;
static const char* SENDSPIN_PATH = "/sendspin";

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

static std::atomic<bool> running{true};

static void signal_handler(int /*sig*/) {
    running.store(false);
}

static void print_usage(const char* prog) {
    fprintf(stderr, "Usage: %s [options] [name]\n", prog);
    fprintf(stderr, "  name          Friendly name (default: \"Basic Client\")\n\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -u URL        Connect to a WebSocket URL (e.g. ws://192.168.1.10:8928/sendspin)\n");
    fprintf(stderr, "  -l LEVEL      Log level: none, error, warn, info (default), debug, verbose\n");
    fprintf(stderr, "  -v            Verbose logging (same as -l verbose)\n");
    fprintf(stderr, "  -q            Quiet logging (same as -l error)\n");
    fprintf(stderr, "  -L            List available audio devices and exit\n");
    fprintf(stderr, "  -d DEVICE     Select audio device by index (use -L to list devices)\n");
    fprintf(stderr, "  -m MIXER      Use ALSA hardware mixer for volume control (format: card:control, e.g., \"1:Digital\")\n");
    fprintf(stderr, "  -c FILE       Use configuration file (default: /etc/sendspin-client/sendspin-client.conf)\n");
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
    int opt;
    while ((opt = getopt(argc, argv, "u:l:vqhd:m:Lc:")) != -1) {
        switch (opt) {
            case 'u':
                connect_url = optarg;
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
            fprintf(stderr, "Loaded configuration from %s\n", effective_config_file.c_str());
            
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

    // Generate unique client ID based on friendly name and hardware info
    std::string client_id = generate_client_id(friendly_name);
    fprintf(stderr, "Generated client ID: %s\n", client_id.c_str());

    // Configure the client
    SendspinClientConfig config;
    config.client_id = client_id;
    config.name = friendly_name;
    config.product_name = "sendspin-cpp host example";
    config.manufacturer = "sendspin-cpp";
    config.software_version = "0.1.0";

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
    
    auto& controller = client.add_controller();
    auto& metadata = client.add_metadata();

    // Suppress unused variable warnings for roles used only for their side effects
    (void)controller;

    // --- Listener implementations ---

    struct BasicPlayerListener : PlayerRoleListener {
#ifdef SENDSPIN_HAS_PORTAUDIO
        PortAudioSink& sink;
        PlayerRole& player;
        int audio_device_index;
        BasicPlayerListener(PortAudioSink& s, PlayerRole& p, int device_index) : sink(s), player(p), audio_device_index(device_index) {}
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
    BasicPlayerListener player_listener(audio_sink, player, audio_device_index);
    audio_sink.on_frames_played = [&player](uint32_t frames, int64_t timestamp) {
        player.notify_audio_played(frames, timestamp);
    };
#else
    BasicPlayerListener player_listener;
#endif
    BasicMetadataListener metadata_listener;
    BasicClientListener client_listener;
    HostNetworkProvider network_provider;

    player.set_listener(&player_listener);
    metadata.set_listener(&metadata_listener);
    client.set_listener(&client_listener);
    client.set_network_provider(&network_provider);

    // Start the server
    fprintf(stderr, "Starting Sendspin basic client on port %u...\n", SENDSPIN_PORT);

    if (!client.start_server()) {
        fprintf(stderr, "Failed to start server\n");
        return 1;
    }

    // Advertise via mDNS
    MdnsAdvertiser mdns;
    if (!mdns.start(friendly_name, SENDSPIN_PORT, SENDSPIN_PATH)) {
        fprintf(stderr, "Warning: mDNS advertisement failed, server still running\n");
        fprintf(stderr, "Connect manually to ws://<this-host>:%u%s\n", SENDSPIN_PORT,
                SENDSPIN_PATH);
    }

    // Auto-connect if a URL was provided via -u
    if (!connect_url.empty()) {
        fprintf(stderr, "Connecting to %s...\n", connect_url.c_str());
        client.connect_to(connect_url);
    }

    fprintf(stderr, "Press Ctrl+C to stop.\n\n");

    // Main loop
    int tick = 0;
    uint8_t last_volume = player.get_volume();
    bool last_muted = player.get_muted();
    while (running.load()) {
        client.loop();
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
#else
        ++tick;
#endif
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    fprintf(stderr, "\nShutting down...\n");
    mdns.stop();
    client.disconnect(SendspinGoodbyeReason::SHUTDOWN);

#ifndef SENDSPIN_HAS_PORTAUDIO
    fprintf(stderr, "Total audio bytes received: %zu\n", null_audio_total_bytes);
#endif
    return 0;
}
