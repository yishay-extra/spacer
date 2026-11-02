#pragma once

#include <string_view>

#include "core/MotionSpec.h"
#include "core/motion/Trajectory.h"

namespace spacer {

/// SPEC §10 `Line`: lerp(from, to, f(t)) in Cartesian space.
/// One period is one traversal from → to. `Once` stops at `to`; `Loop` jumps
/// back to `from`; `PingPong` returns to `from` over the next period.
/// `Smooth` easing is a cubic smoothstep applied to each traversal.
class LineTrajectory final : public Trajectory {
public:
    explicit LineTrajectory(const LineMotion& spec);

    std::string_view name() const override { return "Line"; }
    bool supports(MotionParam p) const override;

private:
    Position compute(const MotionTime& t) override;
    void restart() override;

    LineMotion spec_;
};

}  // namespace spacer
