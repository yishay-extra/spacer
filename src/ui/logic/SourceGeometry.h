#pragma once

// Where a source is drawn on the stage and what dragging it does (SPEC §16
// item 1: "drag sources (sets Manual motion value or offsets the motion
// centre)"). Plain C++, unit-tested on Linux.

#include <optional>
#include <vector>

#include "core/Layout.h"
#include "core/Source.h"

namespace spacer {

/// Number of rendered points of a source: 2 for stereo, else 1 (SPEC §8).
int sourcePointCount(const SourceSpec& source);

/// The motion that drives point `channel` (0 = left / mono, 1 = right):
/// `motionRight` for the right point of a linkedPoints source (falling back
/// to `motion` when it is unset), else `motion`.
const MotionSpec& motionForChannel(const SourceSpec& source, int channel);

/// Left/right point of a stereo pair whose control point is `centre`:
/// azimuth ∓ width/2, same elevation and distance (SPEC §8). Mono: `centre`.
Position stereoPoint(const Position& centre, float widthDeg, int channel, bool stereo);

/// The position of every point of `source` from the model alone, at motion
/// phase 0 (used before the engine reports rendered positions). `layout`
/// resolves step-sequence speaker targets.
std::vector<Position> modelPoints(const SourceSpec& source, const Layout& layout);

/// `p` moved by `delta` metres (Cartesian, SPEC §2).
Position offsetPosition(const Position& p, const Cartesian& delta);

/// `motion` with everything that places it in space moved by `delta`: the
/// Manual position, Orbit / Lissajous centre, Line endpoints, Path points,
/// RandomWalk bounds, StepSequence fixed-position targets (speaker targets
/// stay on their speaker).
MotionSpec offsetMotion(const MotionSpec& motion, const Cartesian& delta);

/// The result of dragging point `channel` of `start` (the spec when the drag
/// began) by `delta` metres. Non-linked sources move as a whole (both points
/// follow the control point); linkedPoints sources move only the dragged
/// point's motion.
SourceSpec dragSource(const SourceSpec& start, int channel, const Cartesian& delta);

/// True if every motion of the source is Manual (positions can then be sent
/// as SetSourcePosition commands instead of rebuilding the graph).
bool isManualOnly(const SourceSpec& source);

}  // namespace spacer
