#pragma once

#include <string_view>

#include "core/MotionSpec.h"
#include "core/motion/Trajectory.h"

namespace spacer {

/// SPEC §10 `Manual`: holds the last set position. A new target set with
/// setTarget() is approached by a straight line in Cartesian space over
/// `smoothMs` of engine time (seconds, so it also runs while the transport is
/// stopped). Modulation (motion.position.*) is added on top of the smoothed
/// position. reset() snaps to the target.
class ManualTrajectory final : public Trajectory {
public:
    explicit ManualTrajectory(const ManualMotion& spec);

    std::string_view name() const override { return "Manual"; }
    bool supports(MotionParam p) const override;

    /// RT-safe. Starts a glide from the current position to `target`.
    void setTarget(const Position& target);
    void setSmoothMs(float ms) { smoothMs_ = ms; }
    const Position& target() const { return target_; }

private:
    Position compute(const MotionTime& t) override;
    void restart() override;
    Position output() const;

    Position target_;
    float smoothMs_;
    Cartesian from_{};
    Cartesian base_{};
    double glideElapsed_{0.0};
    double glideSeconds_{0.0};
};

}  // namespace spacer
