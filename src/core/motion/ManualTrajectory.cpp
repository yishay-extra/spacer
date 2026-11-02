#include "core/motion/ManualTrajectory.h"

#include <algorithm>

namespace spacer {

ManualTrajectory::ManualTrajectory(const ManualMotion& spec)
    : target_(spec.position), smoothMs_(spec.smoothMs) {
    restart();
}

bool ManualTrajectory::supports(MotionParam p) const {
    return p == MotionParam::PositionAz || p == MotionParam::PositionEl || p == MotionParam::PositionDist;
}

void ManualTrajectory::setTarget(const Position& target) {
    target_ = target;
    from_ = base_;
    glideElapsed_ = 0.0;
    glideSeconds_ = std::max(0.0, static_cast<double>(smoothMs_) / 1000.0);
    if (glideSeconds_ <= 0.0) base_ = target_.toCartesian();
}

void ManualTrajectory::restart() {
    base_ = from_ = target_.toCartesian();
    glideElapsed_ = glideSeconds_ = 0.0;
    current_ = output().normalised();
}

Position ManualTrajectory::output() const {
    return modulated(Position::fromCartesian(base_), MotionParam::PositionAz, MotionParam::PositionEl,
                     MotionParam::PositionDist);
}

Position ManualTrajectory::compute(const MotionTime& t) {
    const Cartesian to = target_.toCartesian();
    if (glideSeconds_ > 0.0 && glideElapsed_ < glideSeconds_) {
        glideElapsed_ += std::max(0.0, t.deltaSeconds);
        const auto f = static_cast<float>(std::min(1.0, glideElapsed_ / glideSeconds_));
        base_ = from_ + (to - from_) * f;
    } else {
        base_ = to;
    }
    return output();
}

}  // namespace spacer
