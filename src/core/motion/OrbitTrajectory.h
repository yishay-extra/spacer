#pragma once

#include <string_view>

#include "core/MotionSpec.h"
#include "core/motion/Trajectory.h"

namespace spacer {

/// SPEC §10 `Orbit`: centre + R·(r·sin θ, r·cos θ, 0) with
/// θ = 2π·(t/period)·direction + phaseOffset.
///
/// "Rotated to centre frame": R is the rotation that takes the front axis
/// (+y) to the centre's direction — pitch by the centre's elevation about +x,
/// then yaw by its azimuth (clockwise about +z). With the centre at az 0,
/// el 0 the orbit is horizontal and θ = 0 is in front; direction +1 is
/// clockwise seen from above. The centre's distance only places the centre;
/// a centre at the origin with a non-zero elevation tilts the orbit plane.
///
/// Elevation wobble w = elevationWobble·sin(2π·t/wobblePeriod) lifts the
/// point out of the orbit plane by angle w as seen from the centre:
/// (r·sin θ·cos w, r·cos θ·cos w, r·sin w).
class OrbitTrajectory final : public Trajectory {
public:
    explicit OrbitTrajectory(const OrbitMotion& spec);

    std::string_view name() const override { return "Orbit"; }
    bool supports(MotionParam p) const override;

private:
    Position compute(const MotionTime& t) override;
    void restart() override;

    OrbitMotion spec_;
};

}  // namespace spacer
