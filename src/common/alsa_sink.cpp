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

#include "alsa_sink.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <thread>
#include <vector>

#include <poll.h>

namespace sendspin {

// Target total latency for the PCM (queue + hardware buffer). Generous enough to ride out
// scheduling jitter without underruns; the player's time model compensates through the
// played-frame timestamps this sink reports.
static constexpr useconds_t PCM_LATENCY_US = 50000;

// A hardware mixer write is stepped over this long, so the level does not jump in one click.
static constexpr int MIXER_RAMP_STEPS = 4;
static constexpr int MIXER_RAMP_STEP_MS = 5;

/// @brief Monotonic milliseconds, for spacing mixer ramp steps.
static int64_t mixer_now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// ============================================================================

namespace {

/// @brief Kernel-side "operation not supported" (linux/errno.h ENOTSUPP, 524). Not part of
/// glibc's errno set, so defined here; ALSA returns it through some drivers' open path.
static constexpr int KERNEL_ENOTSUPP = 524;

/// @brief True when SENDSPIN_AUDIO_DEBUG is set in the environment; keeps the library chatter
/// and the [ALSA DEBUG] lines enabled for diagnostics.
bool audio_debug_enabled() {
    static const bool enabled = getenv("SENDSPIN_AUDIO_DEBUG") != nullptr;
    return enabled;
}

/// @brief fprintf-like print gated on SENDSPIN_AUDIO_DEBUG, for the AlsaSink's own
/// [ALSA DEBUG] lines.
void alsa_debug_print(const char* fmt, ...) {
    if (!audio_debug_enabled()) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
}

/// @brief Discard ALSA library errors ("ALSA lib pcm.c:...: Unknown PCM front"). ALSA prints
/// one line per PCM name a device scan probes that the hardware lacks; the failures are
/// expected during enumeration and every return code our code cares about is checked and
/// reported by our own messages.
void quiet_snd_error(const char* /*file*/, int /*line*/, const char* /*function*/, int /*err*/,
                     const char* /*fmt*/, ...) {}

}  // namespace

void install_quiet_audio_logging() {
    if (audio_debug_enabled()) {
        return;
    }
    snd_lib_error_set_handler(quiet_snd_error);
}

// ============================================================================
// AlsaSink
// ============================================================================

AlsaSink::AlsaSink() = default;

AlsaSink::~AlsaSink() {
    stop();
    this->mixer_close_();
}

snd_pcm_format_t AlsaSink::format_for_bits_(uint8_t bits_per_sample) {
    switch (bits_per_sample) {
        case 16:
            return SND_PCM_FORMAT_S16_LE;
        case 24:
            // The wire format is packed 3-byte little-endian samples (what PortAudio's
            // paInt24 carried), not 24-bit-in-32-bit containers.
            return SND_PCM_FORMAT_S24_3LE;
        case 32:
            return SND_PCM_FORMAT_S32_LE;
        default:
            return SND_PCM_FORMAT_UNKNOWN;
    }
}

std::string AlsaSink::resolve_device_(const std::string& raw, bool for_probe) {
    if (raw.empty() || raw == "-1") {
        return "default";
    }
    // Backwards compatibility with the numeric PortAudio device index: a plain number maps
    // to card N, device 0. Probing uses hw: (honest capabilities); playback uses plughw:
    // (format/rate conversion allowed, like PortAudio's default-device path).
    if (!raw.empty() && raw.find_first_not_of("0123456789") == std::string::npos) {
        return (for_probe ? "hw:" : "plughw:") + raw + ",0";
    }
    return raw;
}

bool AlsaSink::configure(uint32_t sample_rate, uint8_t channels, uint8_t bits_per_sample,
                         const std::string& device) {
    if (sample_rate == 0 || channels == 0 ||
        format_for_bits_(bits_per_sample) == SND_PCM_FORMAT_UNKNOWN) {
        fprintf(stderr, "ALSA: unsupported format %uHz %uch %ubit\n", sample_rate, channels,
                bits_per_sample);
        return false;
    }

    std::lock_guard<std::mutex> lock(pcm_mutex_);
    device_ = resolve_device_(device, false);
    sample_rate_ = sample_rate;
    channels_ = channels;
    bits_per_sample_ = bits_per_sample;
    bytes_per_frame_ = static_cast<size_t>(channels) * (bits_per_sample / 8);
    warned_no_pcm_.store(false, std::memory_order_release);
    return open_stream_();
}

bool AlsaSink::open_stream_() {
    close_pcm_();

    snd_pcm_t* pcm = nullptr;
    // Non-blocking mode: drain_() polls with its own deadline, so a poll-ready-but-full
    // race can never block past the on_audio_write() timeout.
    const int err = snd_pcm_open(&pcm, device_.c_str(), SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK);
    if (err < 0) {
        if (err == -KERNEL_ENOTSUPP) {
            // Kernel-side "not supported" (524, not a standard errno). Some drivers,
            // notably vc4-HDMI on the Raspberry Pi, return it when the device is held by
            // another player or the hardware rejects the setup; it does not point at the
            // requested format, so point the user at the usual fixes instead.
            fprintf(stderr,
                    "ALSA: cannot open device '%s': the kernel driver refused it (ENOTSUPP). "
                    "Another player (e.g. Kodi) may be holding the device, or the driver "
                    "rejects the setup; try AUDIO_DEVICE=\"default\" and check with "
                    "'aplay -D %s <wav>'\n",
                    device_.c_str(), device_.c_str());
        } else {
            fprintf(stderr, "ALSA: cannot open device '%s': %s\n", device_.c_str(),
                    snd_strerror(err));
        }
        return false;
    }
    pcm_ = pcm;

    const int set_err = snd_pcm_set_params(pcm_, format_for_bits_(bits_per_sample_),
                                           SND_PCM_ACCESS_RW_INTERLEAVED, channels_, sample_rate_,
                                           1 /* allow soft resample */, PCM_LATENCY_US);
    if (set_err < 0) {
        fprintf(stderr, "ALSA: cannot configure '%s' for %uHz %uch %ubit: %s\n",
                device_.c_str(), sample_rate_, channels_, bits_per_sample_,
                snd_strerror(set_err));
        close_pcm_();
        return false;
    }

    if (snd_pcm_prepare(pcm_) < 0) {
        fprintf(stderr, "ALSA: cannot prepare device '%s'\n", device_.c_str());
        close_pcm_();
        return false;
    }

    fprintf(stderr, "ALSA: playing %uHz %uch %ubit on '%s'\n", sample_rate_, channels_,
            bits_per_sample_, device_.c_str());
    return true;
}

void AlsaSink::close_pcm_() {
    if (pcm_ != nullptr) {
        snd_pcm_close(pcm_);
        pcm_ = nullptr;
    }
}

size_t AlsaSink::write(uint8_t* data, size_t length, uint32_t timeout_ms) {
    if (length == 0) {
        return 0;
    }

    std::lock_guard<std::mutex> lock(pcm_mutex_);
    if (pcm_ == nullptr) {
        // No open PCM (configure() failed or the device was released). Pace the caller the
        // way a full buffer would: returning 0 immediately would hot-spin the sync task.
        // Warn once, so a device that cannot open does not spam the log per 20 ms push.
        if (!warned_no_pcm_.exchange(true)) {
            fprintf(stderr,
                    "Warning: audio device not open; dropping audio until the next stream "
                    "start reopens it\n");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        return 0;
    }

    const size_t consumed = drain_(data, length, timeout_ms);
    if (consumed > 0 && on_frames_played) {
        report_played_(consumed / bytes_per_frame_);
    }
    return consumed;
}

size_t AlsaSink::drain_(uint8_t* data, size_t length, uint32_t timeout_ms) {
    // Callers guarantee frame-aligned pushes; if one is not, round down and drop the
    // trailing partial frame rather than queueing bytes the PCM can never accept.
    const size_t whole_bytes = length - (length % bytes_per_frame_);
    if (whole_bytes == 0) {
        return 0;
    }
    const size_t frames_total = whole_bytes / bytes_per_frame_;

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    const uint64_t vol = volume_multiplier_.load(std::memory_order_relaxed);
    const bool scale = vol < (UINT64_C(1) << 32);
    const uint8_t bytes_per_sample = bits_per_sample_ / 8;
    // Scratch for volume scaling, reused across chunks; allocated once outside the loop.
    std::vector<uint8_t> scratch;
    if (scale) {
        scratch.reserve(whole_bytes);
    }

    size_t frames_done = 0;
    int consecutive_errors = 0;
    while (frames_done < frames_total && pcm_ != nullptr) {
        const long avail = snd_pcm_avail_update(pcm_);
        if (avail < 0) {
            if (!recover_(static_cast<int>(avail)) || ++consecutive_errors > 8) {
                break;
            }
            continue;
        }
        if (avail == 0) {
            if (!wait_for_space_(deadline)) {
                break;
            }
            continue;
        }

        const size_t frames = std::min<size_t>(static_cast<size_t>(avail),
                                               frames_total - frames_done);
        const uint8_t* out = data + frames_done * bytes_per_frame_;
        // Scale a copy, never the caller's buffer: a partial write leaves the source
        // unconsumed and it is re-presented unscaled on the next call.
        if (scale) {
            scratch.assign(out, out + frames * bytes_per_frame_);
            apply_volume_(scratch.data(), scratch.size(), bytes_per_sample, vol);
            out = scratch.data();
        }

        const snd_pcm_sframes_t written = snd_pcm_writei(pcm_, out, frames);
        if (written == static_cast<snd_pcm_sframes_t>(frames)) {
            frames_done += frames;
            consecutive_errors = 0;
            continue;
        }
        if (written > 0) {
            frames_done += static_cast<size_t>(written);
            consecutive_errors = 0;
            continue;
        }
        if (written == -EAGAIN) {
            if (!wait_for_space_(deadline)) {
                break;
            }
            continue;
        }
        if (written < 0 && (!recover_(static_cast<int>(written)) || ++consecutive_errors > 8)) {
            break;
        }
    }

    return frames_done * bytes_per_frame_;
}

bool AlsaSink::wait_for_space_(std::chrono::steady_clock::time_point deadline) {
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline) {
        return false;
    }
    const int timeout_ms = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());

    struct pollfd pfds[8];
    const int count = snd_pcm_poll_descriptors_count(pcm_);
    if (count <= 0 || static_cast<size_t>(count) > sizeof(pfds) / sizeof(pfds[0])) {
        return false;
    }
    if (snd_pcm_poll_descriptors(pcm_, pfds, static_cast<unsigned int>(count)) < 0) {
        return false;
    }
    const int ready = ::poll(pfds, static_cast<nfds_t>(count), timeout_ms);
    if (ready <= 0) {
        return false;
    }
    unsigned short revents = 0;
    snd_pcm_poll_descriptors_revents(pcm_, pfds, static_cast<unsigned int>(count), &revents);
    // POLLOUT means space; POLLERR usually signals an underrun/disconnect, which the
    // next writei/avail_update reports and recover_() handles.
    return (revents & (POLLOUT | POLLERR)) != 0;
}

bool AlsaSink::recover_(int err) {
    if (pcm_ == nullptr) {
        return false;
    }
    if (err == -EPIPE || err == -ESTRPIPE) {
        // silent=1: the recovery is routine (underruns happen when the network stalls);
        // ALSA's own message would only add noise.
        if (snd_pcm_recover(pcm_, err, 1) == 0) {
            alsa_debug_print("[ALSA DEBUG] recovered from %s\n", snd_strerror(err));
            return true;
        }
        return false;
    }
    if (err == -EAGAIN) {
        return true;
    }
    fprintf(stderr, "ALSA: PCM error on '%s': %s\n", device_.c_str(), snd_strerror(err));
    return false;
}

void AlsaSink::report_played_(uint32_t frames) {
    if (frames == 0) {
        return;
    }
    long delay_frames = 0;
    if (snd_pcm_delay(pcm_, &delay_frames) != 0 || delay_frames < 0) {
        return;
    }
    // Finish time: when the last frame of this batch leaves the DAC. The PCM delay counts
    // every queued frame (this batch included) in stream-rate frames. The epoch must be the
    // library's clock, or the offset between the two clocks reads as a sync error.
    const int64_t now_us = clock_us ? clock_us()
                                    : std::chrono::duration_cast<std::chrono::microseconds>(
                                          std::chrono::steady_clock::now().time_since_epoch())
                                          .count();
    const int64_t delay_us = static_cast<int64_t>(delay_frames) * 1000000LL / sample_rate_;
    this->on_frames_played(frames, now_us + delay_us);
}

void AlsaSink::stop() {
    std::lock_guard<std::mutex> lock(pcm_mutex_);
    close_pcm_();
}

void AlsaSink::clear() {
    std::lock_guard<std::mutex> lock(pcm_mutex_);
    if (pcm_ != nullptr) {
        // Drop everything queued and return to PREPARED so the next write starts playback
        // again; the device stays claimed, matching the old ring-buffer clear().
        snd_pcm_drop(pcm_);
        snd_pcm_prepare(pcm_);
    }
}

void AlsaSink::set_volume(uint8_t volume) {
    volume_ = volume > 100 ? 100 : volume;

    // The mixer carries the level when it has the control; the software gain only ever covers
    // what the mixer cannot, so it is recomputed from both fields rather than forced to unity.
    if (mixer_elem_ != nullptr) {
        // A refused write hands the level back to the software gain; the ramp reports success.
        mixer_carries_volume_ =
            start_mixer_ramp_(mixer_dB_for_volume(mixer_dB_min_, mixer_dB_max_, volume_, mixer_curve_));
    }

    update_volume_multiplier_();
}

void AlsaSink::set_muted(bool muted) {
    muted_ = muted;

    // A control without a switch leaves the mute to the software gain below.
    if (mixer_elem_ != nullptr && mixer_has_switch_) {
        mixer_carries_mute_ = snd_mixer_selem_set_playback_switch_all(mixer_elem_, muted ? 0 : 1) >= 0;
        if (mixer_carries_mute_) {
            mixer_reported_muted_ = muted;
        }
    }

    update_volume_multiplier_();
}

void AlsaSink::set_alsa_mixer_spec(const std::string& mixer_spec) {
    alsa_mixer_spec_ = mixer_spec;
    mixer_close_();
    if (!alsa_mixer_spec_.empty()) {
        // Held open for the run, so poll() can watch it for changes made outside this process.
        mixer_open_();
    }
}

void AlsaSink::update_volume_multiplier_() {
    volume_multiplier_.store(software_gain_(), std::memory_order_relaxed);
}

uint64_t AlsaSink::software_gain_() const {
    static constexpr uint64_t Q32_ONE = UINT64_C(1) << 32;
    // Volume 0 is silence per the spec; a control whose floor is only -60 dB cannot reach it, so
    // the PCM gain carries it. This is independent of mute, which stays on the switch.
    if (volume_ == 0) {
        return 0;
    }
    // Mute is the switch's when the mixer took it; otherwise the PCM gain silences. This covers
    // a control with no switch as well as one whose switch write was refused.
    if (muted_ && !mixer_carries_mute_) {
        return 0;
    }
    // The mixer owns the level when it took the write, so the PCM stays at unity. When it did
    // not (no control, or a refused write), the software curve carries the level instead; a
    // control that recovers sets the flag again on its next successful write.
    if (mixer_carries_volume_) {
        return Q32_ONE;
    }
    // Quadratic curve: (vol/100)^2 for perceptually uniform control.
    // Q32 multiplier = vol * vol * 2^32 / 10000
    const uint64_t v = volume_;
    return (v * v * Q32_ONE) / 10000;
}

void AlsaSink::apply_volume_(uint8_t* data, size_t len, uint8_t bytes_per_sample,
                             uint64_t scale) {
    static constexpr int FRAC_BITS = 32;
    static constexpr int64_t ROUND_TERM = INT64_C(1) << (FRAC_BITS - 1);
    int64_t s_scale = static_cast<int64_t>(scale);

    switch (bytes_per_sample) {
        case 1: {
            auto* samples = reinterpret_cast<int8_t*>(data);
            for (size_t i = 0; i < len; ++i) {
                int64_t s = static_cast<int64_t>(samples[i]) * s_scale + ROUND_TERM;
                samples[i] = static_cast<int8_t>(s >> FRAC_BITS);
            }
            break;
        }
        case 2: {
            size_t count = len / 2;
            auto* samples = reinterpret_cast<int16_t*>(data);
            for (size_t i = 0; i < count; ++i) {
                int64_t s = static_cast<int64_t>(samples[i]) * s_scale + ROUND_TERM;
                samples[i] = static_cast<int16_t>(s >> FRAC_BITS);
            }
            break;
        }
        case 3: {
            size_t count = len / 3;
            for (size_t i = 0; i < count; ++i) {
                uint8_t* p = data + i * 3;
                // Unpack 24-bit LE and sign-extend to 32-bit
                int32_t sample = static_cast<int32_t>(p[0] | (p[1] << 8) | (p[2] << 16));
                if (sample & 0x800000) {
                    sample |= static_cast<int32_t>(0xFF000000);
                }
                int32_t out =
                    static_cast<int32_t>((static_cast<int64_t>(sample) * s_scale + ROUND_TERM) >>
                                         FRAC_BITS);
                p[0] = static_cast<uint8_t>(out & 0xFF);
                p[1] = static_cast<uint8_t>((out >> 8) & 0xFF);
                p[2] = static_cast<uint8_t>((out >> 16) & 0xFF);
            }
            break;
        }
        case 4: {
            size_t count = len / 4;
            auto* samples = reinterpret_cast<int32_t*>(data);
            for (size_t i = 0; i < count; ++i) {
                int64_t s = static_cast<int64_t>(samples[i]) * s_scale + ROUND_TERM;
                samples[i] = static_cast<int32_t>(s >> FRAC_BITS);
            }
            break;
        }
        default:
            break;
    }
}

// ============================================================================
// Device and mixer enumeration
// ============================================================================

// ============================================================================
// Format probing and device enumeration
// ============================================================================

bool AlsaSink::is_format_supported(const std::string& device, uint32_t sample_rate,
                                   uint8_t channels, uint8_t bits_per_sample) {
    const snd_pcm_format_t format = format_for_bits_(bits_per_sample);
    if (format == SND_PCM_FORMAT_UNKNOWN || channels == 0) {
        return false;
    }

    snd_pcm_t* pcm = nullptr;
    if (snd_pcm_open(&pcm, resolve_device_(device, true).c_str(), SND_PCM_STREAM_PLAYBACK,
                     SND_PCM_NONBLOCK) < 0) {
        return false;
    }

    // Apply the exact combination before testing: testing format, channels and rate
    // separately against the device's full space passes combinations the device cannot
    // run together, which the client would then advertise and fail to open at play time.
    snd_pcm_hw_params_t* params = nullptr;
    snd_pcm_hw_params_alloca(&params);
    snd_pcm_hw_params_any(pcm, params);
    const bool supported =
        snd_pcm_hw_params_set_access(pcm, params, SND_PCM_ACCESS_RW_INTERLEAVED) == 0 &&
        snd_pcm_hw_params_set_format(pcm, params, format) == 0 &&
        snd_pcm_hw_params_set_channels(pcm, params, channels) == 0 &&
        snd_pcm_hw_params_set_rate(pcm, params, sample_rate, 0) == 0;

    snd_pcm_close(pcm);
    return supported;
}

void AlsaSink::list_devices() {
    fprintf(stderr, "Available ALSA playback devices:\n");
    fprintf(stderr, "  default                    (ALSA default, usually the PulseAudio/PipeWire plugin)\n");

    void** hints = nullptr;
    if (snd_device_name_hint(-1, "pcm", &hints) < 0) {
        fprintf(stderr, "ALSA: could not enumerate devices\n");
        return;
    }

    for (void** hint = hints; *hint != nullptr; ++hint) {
        const char* ioid = snd_device_name_get_hint(*hint, "IOID");
        if (ioid != nullptr && strcmp(ioid, "Output") != 0) {
            free(const_cast<char*>(ioid));
            continue;
        }
        free(const_cast<char*>(ioid));

        const char* name = snd_device_name_get_hint(*hint, "NAME");
        const char* desc = snd_device_name_get_hint(*hint, "DESC");
        if (name != nullptr && strcmp(name, "default") != 0 && strcmp(name, "null") != 0) {
            fprintf(stderr, "  %-26s %s\n", name, desc != nullptr ? desc : "");
        }
        free(const_cast<char*>(name));
        free(const_cast<char*>(desc));
    }
    snd_device_name_free_hint(hints);
}

// ============================================================================
// ALSA hardware volume control
// ============================================================================

bool parse_mixer_curve(const std::string& name, MixerCurve& curve) {
    if (name == "spec") {
        curve = MixerCurve::kSpec;
    } else if (name == "cubic") {
        curve = MixerCurve::kCubic;
    } else {
        return false;
    }
    return true;
}

namespace {

/// @brief dB per volume-decade * 100 for a curve, i.e. the slope of dB = max + slope*log10(v/100).
/// kSpec is 3000 (30 dB/decade); kCubic is 6000 (60 dB/decade).
int mixer_curve_slope_centi_dB(MixerCurve curve) {
    return curve == MixerCurve::kCubic ? 6000 : 3000;
}

/// @brief Read a control's playback dB range, repairing the mute sentinel.
///
/// A control that mutes at its lowest volume step reports that step as
/// SND_CTL_TLV_DB_GAIN_MUTE (-9999999) rather than a real level, so the range comes back as
/// -99999.99 dB .. 0 dB. Used as a curve floor that makes every volume but 100% collapse onto
/// the floor. Rebuild the floor from the raw range and the per-step dB instead, which is where
/// alsamixer gets the same numbers. A range that does not carry the sentinel is left untouched.
void resolve_playback_dB_range(snd_mixer_elem_t* elem, long& min, long& max) {
    if (snd_mixer_selem_get_playback_dB_range(elem, &min, &max) < 0) {
        return;
    }
    if (min != SND_CTL_TLV_DB_GAIN_MUTE) {
        return;
    }
    long raw_min = 0;
    long raw_max = 0;
    if (snd_mixer_selem_get_playback_volume_range(elem, &raw_min, &raw_max) < 0 ||
        raw_max <= raw_min) {
        return;
    }
    long top = 0;
    long previous = 0;
    if (snd_mixer_selem_ask_playback_vol_dB(elem, raw_max, &top) < 0 ||
        snd_mixer_selem_ask_playback_vol_dB(elem, raw_max - 1, &previous) < 0) {
        return;
    }
    const long step = top - previous;
    if (step <= 0) {
        return;
    }
    const long floor = top - (raw_max - raw_min) * step;
    if (floor >= top) {
        return;
    }
    min = floor;
    max = top;
}

}  // namespace

long mixer_dB_for_volume(long min, long max, uint8_t volume, MixerCurve curve) {
    if (volume >= 100) {
        return max;
    }
    if (volume == 0) {
        return min;  // the curve is -inf here; the floor is the closest a control can get
    }
    // The spec's curve, amplitude = (v/100)^1.5, is 30*log10(v/100) dB below max, in 0.01 dB;
    // the cubic (Snapcast) curve is twice that, and its inverse 100*10^(dB/60) is the position
    // alsamixer gives the same control. Volume 0 never reaches the floor on either, which is
    // what the min clamp handles.
    const int slope = mixer_curve_slope_centi_dB(curve);
    const double dB =
        static_cast<double>(max) + slope * std::log10(static_cast<double>(volume) / 100.0);
    return std::max(min, static_cast<long>(std::lround(dB)));
}

uint8_t volume_for_mixer_dB(long min, long max, long dB, MixerCurve curve) {
    if (dB <= min) {
        return 0;
    }
    if (dB >= max) {
        return 100;
    }
    // The inverse of mixer_dB_for_volume().
    const int slope = mixer_curve_slope_centi_dB(curve);
    const double volume = 100.0 * std::pow(10.0, static_cast<double>(dB - max) / slope);
    return static_cast<uint8_t>(std::lround(std::clamp(volume, 0.0, 100.0)));
}

bool AlsaSink::mixer_open_() {
    mixer_ = nullptr;
    mixer_elem_ = nullptr;
    mixer_elem_ = get_alsa_mixer_elem(alsa_mixer_spec_, &mixer_);
    if (mixer_elem_ == nullptr) {
        fprintf(stderr, "Warning: could not open ALSA mixer '%s'; using software volume\n",
                alsa_mixer_spec_.c_str());
        return false;
    }
    if (snd_mixer_selem_get_playback_dB_range(mixer_elem_, &mixer_dB_min_, &mixer_dB_max_) < 0) {
        fprintf(stderr, "Warning: ALSA mixer '%s' has no playback volume; using software volume\n",
                alsa_mixer_spec_.c_str());
        mixer_close_();
        return false;
    }
    // A control that mutes at its lowest step reports the mute sentinel as its floor; rebuild
    // the real one so the curves do not collapse onto it.
    resolve_playback_dB_range(mixer_elem_, mixer_dB_min_, mixer_dB_max_);
    mixer_has_switch_ = snd_mixer_selem_has_playback_switch(mixer_elem_) != 0;
    // Assume the control takes the level until a write says otherwise: starting at unity gain
    // keeps the software path from scaling a level the mixer is already carrying. A write that
    // fails clears the flag, so a control that refuses everything falls back on first use.
    mixer_carries_volume_ = true;
    mixer_carries_mute_ = mixer_has_switch_;
    sync_mixer_reported_();
    return true;
}

void AlsaSink::mixer_close_() {
    if (mixer_ != nullptr) {
        snd_mixer_close(mixer_);
    }
    mixer_ = nullptr;
    mixer_elem_ = nullptr;
    mixer_ramping_ = false;
    mixer_carries_volume_ = false;
    mixer_carries_mute_ = false;
}

void AlsaSink::sync_mixer_reported_() {
    if (mixer_elem_ == nullptr) {
        return;
    }
    long dB = 0;
    if (snd_mixer_selem_get_playback_dB(mixer_elem_, SND_MIXER_SCHN_MONO, &dB) == 0) {
        mixer_reported_dB_ = dB;
    }
    int value = 0;
    mixer_reported_muted_ =
        mixer_has_switch_ &&
        snd_mixer_selem_get_playback_switch(mixer_elem_, SND_MIXER_SCHN_MONO, &value) == 0 &&
        value == 0;
}

bool AlsaSink::write_mixer_dB_(long dB) {
    if (mixer_elem_ == nullptr) {
        mixer_carries_volume_ = false;
        return false;
    }
    if (snd_mixer_selem_set_playback_dB_all(mixer_elem_, dB, 0) < 0) {
        // A refused write hands the level to the software gain; recording it here covers a
        // failure in the middle of a ramp, not just the immediate write.
        mixer_carries_volume_ = false;
        return false;
    }
    mixer_carries_volume_ = true;
    // Store the read-back, not the request: ALSA rounds to the control's step, and the rounded
    // value is what the next event reports. Recording the request would make that echo look like
    // a change made outside this process.
    long actual = dB;
    if (snd_mixer_selem_get_playback_dB(mixer_elem_, SND_MIXER_SCHN_MONO, &actual) == 0) {
        mixer_reported_dB_ = actual;
    } else {
        mixer_reported_dB_ = dB;
    }
    return true;
}

bool AlsaSink::start_mixer_ramp_(long target_dB) {
    if (mixer_elem_ == nullptr) {
        return false;
    }
    // A move of one step or less is not worth a ramp; write it now. Any ramp still in flight is
    // abandoned, or the next poll would resume writing toward its older target and overwrite
    // this newer level.
    const long distance = std::abs(target_dB - mixer_reported_dB_);
    const long step =
        std::max<long>(1, std::abs(mixer_dB_max_ - mixer_dB_min_) / MIXER_RAMP_STEPS);
    if (distance <= step) {
        mixer_ramping_ = false;
        return write_mixer_dB_(target_dB);
    }
    mixer_ramping_ = true;
    // Start from where the control actually stands. On a mute-at-minimum control that is the
    // mute sentinel, well below the repaired floor, so an unclamped start would leave every
    // intermediate step pinned at the mute level and then jump to the target. The reported
    // value itself is left alone, since it is what recognizes an echoed event.
    mixer_ramp_from_dB_ = std::clamp(mixer_reported_dB_, mixer_dB_min_, mixer_dB_max_);
    mixer_ramp_target_dB_ = target_dB;
    mixer_ramp_step_ = 0;
    mixer_ramp_next_ms_ = mixer_now_ms();
    return true;
}

bool AlsaSink::step_mixer_ramp_() {
    if (!mixer_ramping_) {
        return true;
    }
    ++mixer_ramp_step_;
    const double progress = static_cast<double>(mixer_ramp_step_) / MIXER_RAMP_STEPS;
    const long dB = static_cast<long>(std::lround(
        static_cast<double>(mixer_ramp_from_dB_) +
        progress * (static_cast<double>(mixer_ramp_target_dB_) - mixer_ramp_from_dB_)));
    if (mixer_ramp_step_ >= MIXER_RAMP_STEPS) {
        mixer_ramping_ = false;
        return write_mixer_dB_(mixer_ramp_target_dB_);
    }
    mixer_ramp_next_ms_ = mixer_now_ms() + MIXER_RAMP_STEP_MS;
    return write_mixer_dB_(dB);
}

void AlsaSink::report_mixer_change_() {
    if (mixer_elem_ == nullptr) {
        return;
    }
    long dB = 0;
    const bool have_dB =
        snd_mixer_selem_get_playback_dB(mixer_elem_, SND_MIXER_SCHN_MONO, &dB) == 0;
    int value = 0;
    const bool have_switch =
        mixer_has_switch_ &&
        snd_mixer_selem_get_playback_switch(mixer_elem_, SND_MIXER_SCHN_MONO, &value) == 0;
    const bool muted = have_switch && value == 0;
    // Our own write stores its read-back, so any other value is a change made outside this
    // process. A muted control reports its mute-level dB, which is not a volume change.
    const bool dB_changed = have_dB && !muted && dB != mixer_reported_dB_;
    const bool mute_changed = have_switch && muted != mixer_reported_muted_;
    if (!dB_changed && !mute_changed) {
        return;
    }

    // An outside volume change is newer than any ramp in flight; without this the next poll
    // would keep writing toward the ramp's older target and undo it. A mute-only change leaves
    // the ramp alone, so muting mid-ramp still finishes at the level that was asked for.
    if (dB_changed) {
        mixer_reported_dB_ = dB;
        volume_ = volume_for_mixer_dB(mixer_dB_min_, mixer_dB_max_, dB, mixer_curve_);
        // An outside change read back is proof the control holds the level; a control that had
        // fallen back to software gain is used again.
        mixer_carries_volume_ = true;
        mixer_ramping_ = false;
    }
    // Only a control with a switch can report a mute change. Without one, leave the mute where
    // it is: a control whose floor is not silent cannot express mute, so reading it here would
    // silently unmute a stream the user muted.
    if (have_switch) {
        mixer_reported_muted_ = muted;
        muted_ = muted;
        mixer_carries_mute_ = true;
    }
    // A control whose floor is not silent reports volume 0 here; this is what makes it silent.
    update_volume_multiplier_();
    fprintf(stderr, "ALSA mixer '%s' changed to %u%%%s\n", alsa_mixer_spec_.c_str(),
            static_cast<unsigned>(volume_), muted_ ? " (muted)" : "");
    if (on_local_volume_changed) {
        on_local_volume_changed(volume_, muted_);
    }
}

void AlsaSink::poll_mixer_() {
    if (mixer_ == nullptr) {
        return;
    }
    if (mixer_ramping_ && mixer_now_ms() >= mixer_ramp_next_ms_) {
        if (!step_mixer_ramp_()) {
            fprintf(stderr, "Warning: ALSA mixer '%s' stopped taking the volume ramp\n",
                    alsa_mixer_spec_.c_str());
            mixer_ramping_ = false;
            // The failed step cleared the carry flag, so the level is the software gain's now.
            // Without this the multiplier stays at the unity a running ramp uses, and the level
            // sits wherever the control last accepted a write.
            update_volume_multiplier_();
        }
    }

    // poll() with a zero timeout, so a tick never blocks; handle_events() only runs when a
    // descriptor is readable, which is what keeps it from blocking on the control handle.
    const int count = snd_mixer_poll_descriptors_count(mixer_);
    if (count <= 0) {
        return;
    }
    std::array<struct pollfd, 8> pfds{};
    const unsigned int space =
        std::min<unsigned int>(static_cast<unsigned int>(count), pfds.size());
    if (snd_mixer_poll_descriptors(mixer_, pfds.data(), space) <= 0) {
        return;
    }
    if (::poll(pfds.data(), space, 0) <= 0) {
        return;
    }
    // Read the element after draining rather than trusting the event count: the callback chain
    // may report no event for a change, and the read is cheap and echo-suppressed anyway.
    snd_mixer_handle_events(mixer_);
    report_mixer_change_();
}

void AlsaSink::poll() {
    poll_mixer_();
}

snd_mixer_elem_t* AlsaSink::get_alsa_mixer_elem(const std::string& mixer_spec,
                                                snd_mixer_t** handle) {
    // Parse mixer specification in format "card:control"
    size_t colon_pos = mixer_spec.find(':');
    if (colon_pos == std::string::npos) {
        return nullptr;
    }

    std::string card_str = mixer_spec.substr(0, colon_pos);
    std::string control_name = mixer_spec.substr(colon_pos + 1);
    snd_mixer_selem_id_t* sid;

    // Every failure below closes the handle. Clear the caller's pointer as well, or it keeps a
    // dangling value that a later mixer_close_() would free a second time.
    *handle = nullptr;

    // Open mixer
    if (snd_mixer_open(handle, 0) < 0) {
        return nullptr;
    }

    // Attach to the specified card
    std::string card_name = "hw:" + card_str;
    if (snd_mixer_attach(*handle, card_name.c_str()) < 0) {
        snd_mixer_close(*handle);
        *handle = nullptr;
        return nullptr;
    }

    // Register mixer
    if (snd_mixer_selem_register(*handle, nullptr, nullptr) < 0) {
        snd_mixer_close(*handle);
        *handle = nullptr;
        return nullptr;
    }

    // Load mixer elements
    if (snd_mixer_load(*handle) < 0) {
        snd_mixer_close(*handle);
        *handle = nullptr;
        return nullptr;
    }

    // Allocate memory for mixer element ID
    snd_mixer_selem_id_alloca(&sid);
    snd_mixer_selem_id_set_index(sid, 0);
    snd_mixer_selem_id_set_name(sid, control_name.c_str());

    // Find the mixer element
    snd_mixer_elem_t* elem = snd_mixer_find_selem(*handle, sid);
    if (!elem) {
        snd_mixer_close(*handle);
        *handle = nullptr;
        return nullptr;
    }

    return elem;
}

bool AlsaSink::set_alsa_volume(const std::string& mixer_spec, uint8_t volume, MixerCurve curve) {
    alsa_debug_print("[ALSA DEBUG] Setting volume to %d%% for mixer: %s\n", volume, mixer_spec.c_str());

    // Use helper function to get mixer element
    snd_mixer_t* handle;
    snd_mixer_elem_t* elem = get_alsa_mixer_elem(mixer_spec, &handle);
    if (!elem) {
        fprintf(stderr, "Warning: could not open ALSA mixer '%s'; using software volume\n",
                mixer_spec.c_str());
        return false;
    }

    // Set volume (convert 0-100 to ALSA dB range on the spec's curve)
    long alsa_min, alsa_max;
    resolve_playback_dB_range(elem, alsa_min, alsa_max);
    alsa_debug_print("[ALSA DEBUG] dB range: %ld to %ld\n", alsa_min, alsa_max);

    const long alsa_volume = mixer_dB_for_volume(alsa_min, alsa_max, volume, curve);

    alsa_debug_print("[ALSA DEBUG] Target volume %d%% -> dB value: %ld\n", volume, alsa_volume);

    if (snd_mixer_selem_set_playback_dB_all(elem, alsa_volume, 0) < 0) {
        fprintf(stderr, "Warning: setting ALSA mixer '%s' volume failed: %s\n",
                mixer_spec.c_str(), snd_strerror(errno));
        snd_mixer_close(handle);
        return false;
    }

    int current_vol = get_alsa_volume(mixer_spec, curve);
    alsa_debug_print("[ALSA DEBUG] Successfully set volume to %d%% (current: %d)\n", volume, current_vol);

    snd_mixer_close(handle);
    return true;
}

int AlsaSink::get_alsa_volume(const std::string& mixer_spec, MixerCurve curve) {
    alsa_debug_print("[ALSA DEBUG] Getting volume for mixer: %s\n", mixer_spec.c_str());

    // Use helper function to get mixer element
    snd_mixer_t* handle;
    snd_mixer_elem_t* elem = get_alsa_mixer_elem(mixer_spec, &handle);
    if (!elem) {
        fprintf(stderr, "Warning: could not read volume from ALSA mixer '%s'\n",
                mixer_spec.c_str());
        return -1;
    }

    // Get volume
    long alsa_min, alsa_max, alsa_volume;
    resolve_playback_dB_range(elem, alsa_min, alsa_max);
    snd_mixer_selem_get_playback_dB(elem, SND_MIXER_SCHN_MONO, &alsa_volume);

    const int volume = volume_for_mixer_dB(alsa_min, alsa_max, alsa_volume, curve);
    snd_mixer_close(handle);
    return volume;
}

bool AlsaSink::set_alsa_mute(const std::string& mixer_spec, bool muted) {
    // Use helper function to get mixer element
    snd_mixer_t* handle;
    snd_mixer_elem_t* elem = get_alsa_mixer_elem(mixer_spec, &handle);
    if (!elem) {
        fprintf(stderr, "Warning: could not open ALSA mixer '%s'; using software mute\n",
                mixer_spec.c_str());
        return false;
    }

    // Check if this control supports playback switch (mute)
    if (!snd_mixer_selem_has_playback_switch(elem)) {
        snd_mixer_close(handle);
        return false;
    }

    // Set mute state
    if (snd_mixer_selem_set_playback_switch_all(elem, !muted) < 0) {
        snd_mixer_close(handle);
        return false;
    }

    snd_mixer_close(handle);
    return true;
}

bool AlsaSink::get_alsa_mute(const std::string& mixer_spec) {
    // Use helper function to get mixer element
    snd_mixer_t* handle;
    snd_mixer_elem_t* elem = get_alsa_mixer_elem(mixer_spec, &handle);
    if (!elem) {
        fprintf(stderr, "Warning: could not read mute state from ALSA mixer '%s'\n",
                mixer_spec.c_str());
        return false;
    }

    // Check if this control supports playback switch (mute)
    if (!snd_mixer_selem_has_playback_switch(elem)) {
        snd_mixer_close(handle);
        return false;
    }

    // Get mute state
    int mute_state;
    if (snd_mixer_selem_get_playback_switch(elem, SND_MIXER_SCHN_MONO, &mute_state) < 0) {
        snd_mixer_close(handle);
        return false;
    }

    snd_mixer_close(handle);
    return !mute_state;  // Return true if muted (switch is off)
}

}  // namespace sendspin
