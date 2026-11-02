#pragma once

#include <string_view>

#include "core/MotionSpec.h"
#include "core/motion/Trajectory.h"

namespace spacer {

/// SPEC §10 `Lissajous`: centre + (ax·sin(2π·fx·t + φx), ay·sin(2π·fy·t + φy),
/// az·sin(2π·fz·t + φz)), with t in periods and the offset in Cartesian
/// metres (x right, y front, z up). Phases are degrees.
class LissajousTrajectory final : public Trajectory {
public:
    explicit LissajousTrajectory(const LissajousMotion& spec);

    std::string_view name() const override { return "Lissajous"; }
    bool supports(MotionParam p) const override;

private:
    Position compute(const MotionTime& t) override;
    void restart() override;

    LissajousMotion spec_;
};

}  // namespace spacer
