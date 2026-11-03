#include "core/motion/MotionParamValue.h"

#include <variant>

namespace spacer {

namespace {

std::optional<float> positionField(const Position& p, MotionParam param, MotionParam az, MotionParam el,
                                   MotionParam dist) {
    if (param == az) return p.azimuth;
    if (param == el) return p.elevation;
    if (param == dist) return p.distance;
    return std::nullopt;
}

struct Lookup {
    MotionParam p;

    std::optional<float> operator()(const ManualMotion& m) const {
        return positionField(m.position, p, MotionParam::PositionAz, MotionParam::PositionEl,
                             MotionParam::PositionDist);
    }
    std::optional<float> operator()(const OrbitMotion& m) const {
        if (auto v = positionField(m.centre, p, MotionParam::CentreAz, MotionParam::CentreEl,
                                   MotionParam::CentreDist))
            return v;
        switch (p) {
            case MotionParam::Radius:
                return m.radius;
            case MotionParam::PhaseOffset:
                return m.phaseOffset;
            case MotionParam::ElevationWobble:
                return m.elevationWobble;
            default:
                return std::nullopt;
        }
    }
    std::optional<float> operator()(const LineMotion& m) const {
        if (auto v =
                positionField(m.from, p, MotionParam::FromAz, MotionParam::FromEl, MotionParam::FromDist))
            return v;
        return positionField(m.to, p, MotionParam::ToAz, MotionParam::ToEl, MotionParam::ToDist);
    }
    std::optional<float> operator()(const LissajousMotion& m) const {
        if (auto v = positionField(m.centre, p, MotionParam::CentreAz, MotionParam::CentreEl,
                                   MotionParam::CentreDist))
            return v;
        switch (p) {
            case MotionParam::Ax:
                return m.ax;
            case MotionParam::Ay:
                return m.ay;
            case MotionParam::Az:
                return m.az;
            case MotionParam::PhaseX:
                return m.phases[0];
            case MotionParam::PhaseY:
                return m.phases[1];
            case MotionParam::PhaseZ:
                return m.phases[2];
            default:
                return std::nullopt;
        }
    }
    std::optional<float> operator()(const PathMotion&) const { return std::nullopt; }
    std::optional<float> operator()(const RandomWalkMotion& m) const {
        if (p == MotionParam::StepMetres) return m.stepMetres;
        return std::nullopt;
    }
    std::optional<float> operator()(const StepSequenceMotion&) const { return std::nullopt; }
};

}  // namespace

std::optional<float> motionParamValue(const MotionSpec& spec, MotionParam param) {
    return std::visit(Lookup{param}, spec.value);
}

}  // namespace spacer
