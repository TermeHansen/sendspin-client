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

// The volume-to-dB curve behind the -m hardware mixer. A self-contained check with no test
// framework, because the repo's only other test is a full integration test.

#include "common/alsa_sink.h"

#include <cstdint>
#include <cstdio>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

// A control whose range runs from -60 dB to 0 dB, in hundredths of a dB.
constexpr long MIN_DB = -6000;
constexpr long MAX_DB = 0;

}  // namespace

int main() {
    using sendspin::mixer_dB_for_volume;
    using sendspin::volume_for_mixer_dB;
    using sendspin::MixerCurve;
    using sendspin::parse_mixer_curve;

    check(mixer_dB_for_volume(MIN_DB, MAX_DB, 100) == MAX_DB, "full volume is the control max");
    check(mixer_dB_for_volume(MIN_DB, MAX_DB, 0) == MIN_DB, "zero volume is the control floor");
    // The spec's (v/100)^1.5 is 30*log10(v/100) dB; at 50 that is -9.03 dB, not -18.
    check(mixer_dB_for_volume(MIN_DB, MAX_DB, 50) == -903, "follows the spec 30*log10 slope");
    // 30*log10(0.1) is -30 dB, below a floor of -20 dB, so the floor wins.
    check(mixer_dB_for_volume(-2000, MAX_DB, 10) == -2000, "clamps to the control floor");

    // 1 is left out: its dB lands on the floor, which the inverse reads back as 0.
    for (uint8_t volume : {2, 25, 50, 75, 99, 100}) {
        const long dB = mixer_dB_for_volume(MIN_DB, MAX_DB, volume);
        check(volume_for_mixer_dB(MIN_DB, MAX_DB, dB) == volume, "round-trips through the inverse");
    }

    check(volume_for_mixer_dB(MIN_DB, MAX_DB, MIN_DB) == 0, "inverse clamps at the floor");
    check(volume_for_mixer_dB(MIN_DB, MAX_DB, MIN_DB - 100) == 0, "inverse clamps below the floor");
    check(volume_for_mixer_dB(MIN_DB, MAX_DB, MAX_DB) == 100, "inverse clamps at the max");
    check(volume_for_mixer_dB(MIN_DB, MAX_DB, MAX_DB + 100) == 100, "inverse clamps above the max");

    // The cubic (Snapcast) curve is twice the spec slope in dB: -18.06 dB at 50 on a 60 dB range.
    check(mixer_dB_for_volume(MIN_DB, MAX_DB, 50, MixerCurve::kCubic) == -1806,
          "cubic follows the 60*log10 slope");

    // The linear curve is dB linear in volume: -30 dB at 50 on a -60..0 range.
    check(mixer_dB_for_volume(MIN_DB, MAX_DB, 50, MixerCurve::kLinear) == -3000,
          "linear is dB linear in volume");
    check(mixer_dB_for_volume(MIN_DB, MAX_DB, 25, MixerCurve::kLinear) == -1500,
          "linear quarter point");
    check(mixer_dB_for_volume(MIN_DB, MAX_DB, 100, MixerCurve::kLinear) == MAX_DB,
          "linear full volume is the control max");
    check(mixer_dB_for_volume(MIN_DB, MAX_DB, 0, MixerCurve::kLinear) == MIN_DB,
          "linear zero volume is the control floor");

    // Every curve round-trips through its inverse, at volumes above where the steeper curves
    // would clamp to the floor (the cubic curve hits -60 dB by volume 10 on this range).
    for (MixerCurve curve : {MixerCurve::kSpec, MixerCurve::kLinear, MixerCurve::kCubic}) {
        for (uint8_t volume : {20, 25, 50, 75, 99, 100}) {
            const long dB = mixer_dB_for_volume(MIN_DB, MAX_DB, volume, curve);
            check(volume_for_mixer_dB(MIN_DB, MAX_DB, dB, curve) == volume,
                  "round-trips through the inverse on every curve");
        }
    }

    MixerCurve parsed = MixerCurve::kCubic;
    check(parse_mixer_curve("spec", parsed) && parsed == MixerCurve::kSpec, "parses 'spec'");
    check(parse_mixer_curve("linear", parsed) && parsed == MixerCurve::kLinear, "parses 'linear'");
    check(parse_mixer_curve("cubic", parsed) && parsed == MixerCurve::kCubic, "parses 'cubic'");
    check(!parse_mixer_curve("bogus", parsed), "rejects an unknown curve name");

    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("mixer curve: all checks passed\n");
    return 0;
}
