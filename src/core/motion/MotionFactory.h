#pragma once

#include <memory>

#include "core/Layout.h"
#include "core/MotionSpec.h"
#include "core/motion/Trajectory.h"

namespace spacer {

/// Builds the evaluator for a MotionSpec (SPEC §10). Non-RT: allocates.
/// `layout` resolves StepSequence speaker targets; it may be null, in which
/// case speaker-targeted steps are unusable (always skipped).
/// The returned motion is at phase 0; its time origin is the first block it
/// evaluates.
struct MotionFactory {
    static std::unique_ptr<Trajectory> create(const MotionSpec& spec, const Layout* layout = nullptr);
};

}  // namespace spacer
