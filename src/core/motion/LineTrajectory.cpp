#include "core/motion/LineTrajectory.h"

namespace spacer {

LineTrajectory::LineTrajectory(const LineMotion& spec) : spec_(spec) {
    restart();
}

bool LineTrajectory::supports(MotionParam p) const {
    switch (p) {
        case MotionParam::FromAz:
        case MotionParam::FromEl:
        case MotionParam::FromDist:
        case MotionParam::ToAz:
        case MotionParam::ToEl:
        case MotionParam::ToDist:
            return true;
        default:
            return false;
    }
}

void LineTrajectory::restart() {
    current_ = compute(MotionTime{}).normalised();
}

Position LineTrajectory::compute(const MotionTime& t) {
    auto u = static_cast<float>(playProgress(cyclesOf(t, spec_.period), spec_.mode));
    if (spec_.easing == Easing::Smooth) u = smoothStep(u);
    const Position from =
        modulated(spec_.from, MotionParam::FromAz, MotionParam::FromEl, MotionParam::FromDist);
    const Position to = modulated(spec_.to, MotionParam::ToAz, MotionParam::ToEl, MotionParam::ToDist);
    return lerp(from, to, u);
}

}  // namespace spacer
