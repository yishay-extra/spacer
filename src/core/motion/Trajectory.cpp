#include "core/motion/Trajectory.h"

#include <cmath>

namespace spacer {

void Trajectory::reset() {
    timeline_.restart();
    restart();
}

Position Trajectory::evaluate(const MotionContext& ctx) {
    const MotionTime t = timeline_.advance(ctx);
    const Position p = compute(t);
    // A non-finite result (e.g. from a bad modulation value) would poison the
    // spatializer; hold the previous position instead.
    if (std::isfinite(p.azimuth) && std::isfinite(p.elevation) && std::isfinite(p.distance))
        current_ = p.normalised();
    return current_;
}

}  // namespace spacer
