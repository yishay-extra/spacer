#include "core/motion/ParameterPath.h"

#include <array>

namespace spacer {

namespace {

struct Entry {
    std::string_view name;
    ParameterPath path;
};

constexpr ParameterPath src(SourceParam p) {
    return {ParameterPath::Domain::Source, p, MotionParam::Radius};
}
constexpr ParameterPath mot(MotionParam p) {
    return {ParameterPath::Domain::Motion, SourceParam::GainDb, p};
}

constexpr std::array kEntries{
    Entry{"gain", src(SourceParam::GainDb)},
    Entry{"spread", src(SourceParam::Spread)},
    Entry{"width", src(SourceParam::Width)},
    Entry{"distance.k", src(SourceParam::DistanceK)},
    Entry{"distance.rMin", src(SourceParam::DistanceRMin)},
    Entry{"distance.q", src(SourceParam::DistanceQ)},
    Entry{"motion.position.az", mot(MotionParam::PositionAz)},
    Entry{"motion.position.el", mot(MotionParam::PositionEl)},
    Entry{"motion.position.dist", mot(MotionParam::PositionDist)},
    Entry{"motion.centre.az", mot(MotionParam::CentreAz)},
    Entry{"motion.centre.el", mot(MotionParam::CentreEl)},
    Entry{"motion.centre.dist", mot(MotionParam::CentreDist)},
    Entry{"motion.radius", mot(MotionParam::Radius)},
    Entry{"motion.phaseOffset", mot(MotionParam::PhaseOffset)},
    Entry{"motion.elevationWobble", mot(MotionParam::ElevationWobble)},
    Entry{"motion.from.az", mot(MotionParam::FromAz)},
    Entry{"motion.from.el", mot(MotionParam::FromEl)},
    Entry{"motion.from.dist", mot(MotionParam::FromDist)},
    Entry{"motion.to.az", mot(MotionParam::ToAz)},
    Entry{"motion.to.el", mot(MotionParam::ToEl)},
    Entry{"motion.to.dist", mot(MotionParam::ToDist)},
    Entry{"motion.ax", mot(MotionParam::Ax)},
    Entry{"motion.ay", mot(MotionParam::Ay)},
    Entry{"motion.az", mot(MotionParam::Az)},
    Entry{"motion.phases.x", mot(MotionParam::PhaseX)},
    Entry{"motion.phases.y", mot(MotionParam::PhaseY)},
    Entry{"motion.phases.z", mot(MotionParam::PhaseZ)},
    Entry{"motion.stepMetres", mot(MotionParam::StepMetres)},
};

static_assert(kEntries.size() == kNumSourceParams + kNumMotionParams, "every parameter needs a path");

}  // namespace

std::optional<ParameterPath> ParameterPath::parse(std::string_view path) {
    for (const auto& e : kEntries)
        if (e.name == path) return e.path;
    return std::nullopt;
}

std::string_view ParameterPath::toString() const {
    for (const auto& e : kEntries) {
        if (e.path.domain != domain) continue;
        if (domain == Domain::Source ? e.path.source == source : e.path.motion == motion) return e.name;
    }
    return {};
}

}  // namespace spacer
