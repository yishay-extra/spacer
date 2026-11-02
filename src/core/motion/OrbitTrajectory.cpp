#include "core/motion/OrbitTrajectory.h"

#include <algorithm>
#include <cmath>

namespace spacer {

OrbitTrajectory::OrbitTrajectory(const OrbitMotion& spec) : spec_(spec) {
    restart();
}

bool OrbitTrajectory::supports(MotionParam p) const {
    switch (p) {
        case MotionParam::CentreAz:
        case MotionParam::CentreEl:
        case MotionParam::CentreDist:
        case MotionParam::Radius:
        case MotionParam::PhaseOffset:
        case MotionParam::ElevationWobble:
            return true;
        default:
            return false;
    }
}

void OrbitTrajectory::restart() {
    current_ = compute(MotionTime{}).normalised();
}

Position OrbitTrajectory::compute(const MotionTime& t) {
    const Position centre =
        modulated(spec_.centre, MotionParam::CentreAz, MotionParam::CentreEl, MotionParam::CentreDist);
    const float radius = std::max(0.0f, spec_.radius + mod(MotionParam::Radius));
    const float dir = spec_.direction < 0 ? -1.0f : 1.0f;

    const double turns = fractional(cyclesOf(t, spec_.period));
    const float theta = 2.0f * kPi * static_cast<float>(turns) * dir +
                        degToRad(spec_.phaseOffset + mod(MotionParam::PhaseOffset));
    const float wobbleDeg =
        (spec_.elevationWobble + mod(MotionParam::ElevationWobble)) *
        std::sin(2.0f * kPi * static_cast<float>(fractional(cyclesOf(t, spec_.wobblePeriod))));
    const float w = degToRad(wobbleDeg);

    // Point in the centre's frame: x = right, y = forward, z = up.
    const float lx = radius * std::sin(theta) * std::cos(w);
    const float ly = radius * std::cos(theta) * std::cos(w);
    const float lz = radius * std::sin(w);

    // Pitch by elevation about +x, then yaw by azimuth (clockwise) about +z.
    const float el = degToRad(centre.elevation);
    const float az = degToRad(centre.azimuth);
    const float py = ly * std::cos(el) - lz * std::sin(el);
    const float pz = ly * std::sin(el) + lz * std::cos(el);
    const Cartesian offset{lx * std::cos(az) + py * std::sin(az), -lx * std::sin(az) + py * std::cos(az), pz};

    const Position c{centre.azimuth, centre.elevation, std::max(0.0f, centre.distance)};
    return Position::fromCartesian(c.toCartesian() + offset);
}

}  // namespace spacer
