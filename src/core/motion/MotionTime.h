#pragma once

#include <algorithm>
#include <cmath>

#include "core/MotionSpec.h"

namespace spacer {

/// Time as seen by a motion for one block, in both units (SPEC §9/§10).
/// `beats` and `seconds` are measured from the motion's time origin (set by
/// reset()) to the END of the block. Beats come from `ClockState::beat`, so
/// they follow tempo changes and stand still while the transport is stopped;
/// seconds come from `ClockState::timeSeconds` and always run.
/// The deltas are measured from the end of the previous block.
struct MotionTime {
    double beats{0.0};
    double seconds{0.0};
    double deltaBeats{0.0};
    double deltaSeconds{0.0};

    double elapsed(Period::Unit u) const { return u == Period::Unit::Beats ? beats : seconds; }
    double delta(Period::Unit u) const { return u == Period::Unit::Beats ? deltaBeats : deltaSeconds; }
};

/// Number of periods elapsed (fractional). 0 for a non-positive period.
inline double cyclesOf(const MotionTime& t, const Period& p) {
    return p.value > 0.0 ? t.elapsed(p.unit) / p.value : 0.0;
}

/// x − floor(x), in [0, 1).
inline double fractional(double x) {
    const double f = x - std::floor(x);
    return f < 1.0 ? f : 0.0;
}

/// Maps elapsed periods to a 0..1 progress for a play mode. One period is one
/// traversal from start to end, so a ping-pong round trip takes two periods.
inline double playProgress(double cycles, PlayMode mode) {
    switch (mode) {
        case PlayMode::Once:
            return std::clamp(cycles, 0.0, 1.0);
        case PlayMode::Loop:
            return fractional(cycles);
        case PlayMode::PingPong: {
            const double w = 2.0 * fractional(cycles * 0.5);
            return w <= 1.0 ? w : 2.0 - w;
        }
    }
    return 0.0;
}

/// Cubic smoothstep, 3u² − 2u³.
inline float smoothStep(float u) {
    return u * u * (3.0f - 2.0f * u);
}

}  // namespace spacer
