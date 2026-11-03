#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "core/motion/MotionParam.h"

namespace spacer {

/// A resolved parameter path (SPEC §10), e.g. "gain", "spread",
/// "distance.k", "motion.radius", "motion.centre.az".
///
/// Recognised paths:
///   gain, spread, width, distance.k, distance.rMin, distance.q
///   motion.position.{az,el,dist}             (Manual)
///   motion.centre.{az,el,dist}               (Orbit, Lissajous)
///   motion.radius, motion.phaseOffset, motion.elevationWobble   (Orbit)
///   motion.from.{az,el,dist}, motion.to.{az,el,dist}             (Line)
///   motion.ax, motion.ay, motion.az, motion.phases.{x,y,z}      (Lissajous)
///   motion.stepMetres                                            (RandomWalk)
///   motionRight.<any motion.* tail>    the right point's motion of a
///                                      linkedPoints source (JSON `motionRight`)
///   insert.{slot}.param.{index}        the source's insert parameter (SPEC §13),
///                                      normalised 0..1
///
/// Names match the JSON keys of the model (SPEC §15). Matching is exact and
/// case-sensitive.
struct ParameterPath {
    enum class Domain { Source, Motion, MotionRight, Insert };

    Domain domain{Domain::Source};
    SourceParam source{SourceParam::GainDb};  // valid when domain == Source
    MotionParam motion{MotionParam::Radius};  // valid when domain == Motion / MotionRight
    int slot{0};                              // valid when domain == Insert
    int index{0};                             // valid when domain == Insert

    /// nullopt for an unknown path. Does not allocate.
    static std::optional<ParameterPath> parse(std::string_view path);

    /// Canonical string for this path (static storage). Empty for Insert
    /// paths (use toPathString()).
    std::string_view toString() const;
    /// Canonical string for any path (allocates; not for the audio thread).
    std::string toPathString() const;

    bool operator==(const ParameterPath&) const = default;
};

}  // namespace spacer
