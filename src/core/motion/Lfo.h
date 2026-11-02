#pragma once

#include <cstdint>

#include "core/MotionSpec.h"

namespace spacer {

/// Bipolar LFO waveform value in [−1, 1] at `cycles` periods since phase 0
/// (SPEC §10 modulator source `lfo{shape, period}`). Pure function, RT-safe.
///
///   Sine          sin(2π·p), starts at 0 rising
///   Triangle      0 → 1 at p=¼ → −1 at p=¾ → 0, starts at 0 rising
///   Saw           2p − 1, rising ramp
///   Square        +1 for p < ½, −1 otherwise
///   SampleAndHold uniform random value held for each whole period, from a
///                 stateless hash of (seed, period index) — deterministic and
///                 independent of block size
///
/// where p = fractional part of `cycles`.
float lfoValue(LfoShape shape, double cycles, std::uint32_t seed = 1);

}  // namespace spacer
