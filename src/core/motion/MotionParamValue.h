#pragma once

#include <optional>

#include "core/MotionSpec.h"
#include "core/motion/MotionParam.h"

namespace spacer {

/// The value a motion parameter has in `spec`, in the parameter's own units
/// (degrees, metres), or nullopt if the spec's kind has no such parameter
/// (SPEC §10). Lets a live absolute edit (`SetMotionParam`, OSC
/// `/motion/{param}`) be turned into an additive offset over the spec.
std::optional<float> motionParamValue(const MotionSpec& spec, MotionParam param);

/// The period that sets the phase of a time-driven motion kind (Orbit, Line,
/// Lissajous, Path: `period`; RandomWalk: `stepPeriod`); nullopt for Manual
/// and StepSequence (state machines). Used to re-anchor a live period edit.
std::optional<Period> primaryPeriod(const MotionSpec& spec);

}  // namespace spacer
