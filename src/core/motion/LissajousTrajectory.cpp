#include "core/motion/LissajousTrajectory.h"

#include <algorithm>
#include <cmath>

namespace spacer {

LissajousTrajectory::LissajousTrajectory(const LissajousMotion& spec) : spec_(spec) {
    restart();
}

bool LissajousTrajectory::supports(MotionParam p) const {
    switch (p) {
        case MotionParam::CentreAz:
        case MotionParam::CentreEl:
        case MotionParam::CentreDist:
        case MotionParam::Ax:
        case MotionParam::Ay:
        case MotionParam::Az:
        case MotionParam::PhaseX:
        case MotionParam::PhaseY:
        case MotionParam::PhaseZ:
            return true;
        default:
            return false;
    }
}

void LissajousTrajectory::restart() {
    current_ = compute(MotionTime{}).normalised();
}

Position LissajousTrajectory::compute(const MotionTime& t) {
    const double cycles = cyclesOf(t, spec_.period);
    // Reduce frequency·cycles to one turn in double before going to float, so
    // precision does not degrade as time grows.
    auto axis = [cycles](float amp, float freq, float phaseDeg) {
        const auto turns = static_cast<float>(fractional(static_cast<double>(freq) * cycles));
        return amp * std::sin(2.0f * kPi * turns + degToRad(phaseDeg));
    };
    const Cartesian offset{
        axis(spec_.ax + mod(MotionParam::Ax), spec_.fx, spec_.phases[0] + mod(MotionParam::PhaseX)),
        axis(spec_.ay + mod(MotionParam::Ay), spec_.fy, spec_.phases[1] + mod(MotionParam::PhaseY)),
        axis(spec_.az + mod(MotionParam::Az), spec_.fz, spec_.phases[2] + mod(MotionParam::PhaseZ))};
    const Position centre =
        modulated(spec_.centre, MotionParam::CentreAz, MotionParam::CentreEl, MotionParam::CentreDist);
    const Position c{centre.azimuth, centre.elevation, std::max(0.0f, centre.distance)};
    return Position::fromCartesian(c.toCartesian() + offset);
}

}  // namespace spacer
