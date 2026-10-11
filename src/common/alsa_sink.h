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

#pragma once

#include <alsa/asoundlib.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>

namespace sendspin {

/// @brief Install a quiet ALSA error handler for the process.
///
/// ALSA prints one "ALSA lib ..." line per PCM name a device scan probes that the hardware
/// lacks; during startup enumeration this buries the useful log in dozens of lines. The
/// handler installed here discards that output; every return code is still checked and
/// reported by our own messages.
///
/// Set SENDSPIN_AUDIO_DEBUG=1 in the environment to keep the library output (and the
/// AlsaSink's own [ALSA DEBUG] lines) for diagnostics.
void install_quiet_audio_logging();

/// @brief Plays audio directly through ALSA (libasound).
///
/// Bridges the push model (PlayerRoleListener::on_audio_write) with ALSA's blocking write
/// model. write() drains the caller's buffer into the PCM in whole frames, waiting on the
/// PCM's poll descriptors so the timeout_ms contract is honored without a ring buffer or a
/// callback thread. Hardware volume/mute and their software fallback live in the same
/// places as before (ALSA mixer via the -m spec, Q32 software scaling otherwise).
class AlsaSink {
public:
    AlsaSink();
    ~AlsaSink();

    // Not copyable or movable (the PCM handle and the callbacks hold pointers to this)
    AlsaSink(const AlsaSink&) = delete;
    AlsaSink& operator=(const AlsaSink&) = delete;

    /// @brief Writes decoded PCM audio data to the ALSA PCM.
    /// @return Number of bytes consumed; less than length only on timeout or error.
    size_t write(uint8_t* data, size_t length, uint32_t timeout_ms);

    /// @brief Configure audio format and (re)open the ALSA PCM.
    /// @param device ALSA device string. Empty, "-1" or a plain card number (mapped to
    ///               plughw:N,0) is accepted; empty means the default device.
    bool configure(uint32_t sample_rate, uint8_t channels, uint8_t bits_per_sample,
                   const std::string& device = "");

    /// @brief Stop playback, drop any queued PCM data and close the device.
    void stop();

    /// @brief Discard all buffered audio without closing the PCM.
    void clear();

    /// @brief Set the playback volume (0-100, where 100 = full scale).
    void set_volume(uint8_t volume);

    /// @brief Set the mute state. When muted, output is silenced regardless of volume.
    void set_muted(bool muted);

    /// @brief Set the ALSA mixer specification for hardware volume control.
    /// @param mixer_spec Mixer specification in format "card:control" (e.g., "1:Digital")
    void set_alsa_mixer_spec(const std::string& mixer_spec);

    /// @brief Check if an ALSA PCM supports a given format natively.
    /// @param device ALSA device string; same semantics as configure(). A plain card number
    ///               is probed as hw:N,0 so the plug layer cannot claim rates it would only
    ///               reach by resampling.
    static bool is_format_supported(const std::string& device, uint32_t sample_rate,
                                    uint8_t channels, uint8_t bits_per_sample);

    /// @brief List ALSA PCM playback devices (card/device enumeration, aplay -l style).
    static void list_devices();

    /// @brief Set hardware volume using ALSA mixer.
    /// @param mixer_spec Mixer specification in format "card:control" (e.g., "1:Digital")
    /// @return true if successful, false otherwise.
    static bool set_alsa_volume(const std::string& mixer_spec, uint8_t volume);

    /// @brief Get hardware volume using ALSA mixer.
    /// @param mixer_spec Mixer specification in format "card:control" (e.g., "1:Digital")
    /// @return volume in 0-100 range, or -1 if failed.
    static int get_alsa_volume(const std::string& mixer_spec);

    /// @brief Set hardware mute state using ALSA mixer.
    /// @param mixer_spec Mixer specification in format "card:control" (e.g., "1:Digital")
    /// @return true if successful, false otherwise.
    static bool set_alsa_mute(const std::string& mixer_spec, bool muted);

    /// @brief Get hardware mute state using ALSA mixer.
    /// @param mixer_spec Mixer specification in format "card:control" (e.g., "1:Digital")
    /// @return true if muted, false if unmuted, error will return false (assume unmuted)
    static bool get_alsa_mute(const std::string& mixer_spec);

    /// @brief Callback for timing feedback. Wire to client.notify_audio_played().
    std::function<void(uint32_t frames, int64_t timestamp)> on_frames_played;

private:
    /// @brief Helper to parse "card:control" and open the mixer element.
    /// @return snd_mixer_elem_t* on success, nullptr on failure (handle closed again).
    static snd_mixer_elem_t* get_alsa_mixer_elem(const std::string& mixer_spec,
                                                 snd_mixer_t** handle);

    /// @brief Map a user-facing device selection to an ALSA PCM name.
    /// @param raw Empty, "-1", a plain card number or a full device string.
    /// @param for_probe Plain card numbers map to hw:N,0 for probing (honest capabilities)
    ///                  and plughw:N,0 for playback (conversion allowed).
    static std::string resolve_device_(const std::string& raw, bool for_probe);

    /// @brief ALSA sample format for a bit depth; SND_PCM_FORMAT_UNKNOWN if unsupported.
    static snd_pcm_format_t format_for_bits_(uint8_t bits_per_sample);

    /// @brief Open the PCM for the current format fields and start it.
    /// @return true on success, false with a reported error otherwise.
    bool open_stream_();

    /// @brief Close the PCM if open. Called with pcm_mutex_ held.
    void close_pcm_();

    /// @brief Blocking drain of data into the PCM, honoring timeout_ms.
    /// @return Number of bytes consumed; 0 only when the deadline passed or the PCM failed.
    size_t drain_(uint8_t* data, size_t length, uint32_t timeout_ms);

    /// @brief Wait until the PCM has space again or the deadline passes.
    /// @return true if space may be available, false on timeout/error (caller re-checks).
    bool wait_for_space_(std::chrono::steady_clock::time_point deadline);

    /// @brief Recover the PCM from an error/underrun state; true if playback can continue.
    bool recover_(int err);

    /// @brief Report frames that have left this sink to the player, using the PCM delay.
    void report_played_(uint32_t frames);

    snd_pcm_t* pcm_{nullptr};
    std::string device_;
    uint32_t sample_rate_{0};
    uint8_t channels_{0};
    uint8_t bits_per_sample_{0};
    size_t bytes_per_frame_{0};

    // Serializes configure()/stop()/clear() against write() on the sync-task thread, and
    // protects the PCM handle pointer. The ALSA library is not thread-safe for concurrent
    // calls on one handle. write() holds it while draining; the library's on_audio_write
    // timeout (20 ms) bounds how long the main loop can wait behind it.
    std::mutex pcm_mutex_;

    // Software volume control using Q32 fixed-point math.
    // UINT32_MAX = 1.0 (full scale), 0 = silence. Updated atomically so the write path
    // can read it without extra locking.
    std::atomic<uint64_t> volume_multiplier_{UINT64_C(1) << 32};
    uint8_t volume_{100};
    bool muted_{false};
    std::string alsa_mixer_spec_;
    void update_volume_multiplier_();
    static void apply_volume_(uint8_t* data, size_t len, uint8_t bytes_per_sample, uint64_t scale);
};

}  // namespace sendspin
