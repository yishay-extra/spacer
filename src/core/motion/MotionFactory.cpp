#include "core/motion/MotionFactory.h"

#include <variant>

#include "core/motion/LineTrajectory.h"
#include "core/motion/LissajousTrajectory.h"
#include "core/motion/ManualTrajectory.h"
#include "core/motion/OrbitTrajectory.h"
#include "core/motion/PathTrajectory.h"
#include "core/motion/RandomWalkTrajectory.h"
#include "core/motion/StepSequenceTrajectory.h"

namespace spacer {

namespace {

struct Builder {
    const Layout* layout;
    std::unique_ptr<Trajectory> operator()(const ManualMotion& m) const {
        return std::make_unique<ManualTrajectory>(m);
    }
    std::unique_ptr<Trajectory> operator()(const OrbitMotion& m) const {
        return std::make_unique<OrbitTrajectory>(m);
    }
    std::unique_ptr<Trajectory> operator()(const LineMotion& m) const {
        return std::make_unique<LineTrajectory>(m);
    }
    std::unique_ptr<Trajectory> operator()(const LissajousMotion& m) const {
        return std::make_unique<LissajousTrajectory>(m);
    }
    std::unique_ptr<Trajectory> operator()(const PathMotion& m) const {
        return std::make_unique<PathTrajectory>(m);
    }
    std::unique_ptr<Trajectory> operator()(const RandomWalkMotion& m) const {
        return std::make_unique<RandomWalkTrajectory>(m);
    }
    std::unique_ptr<Trajectory> operator()(const StepSequenceMotion& m) const {
        return std::make_unique<StepSequenceTrajectory>(m, layout);
    }
};

}  // namespace

std::unique_ptr<Trajectory> MotionFactory::create(const MotionSpec& spec, const Layout* layout) {
    return std::visit(Builder{layout}, spec.value);
}

}  // namespace spacer
