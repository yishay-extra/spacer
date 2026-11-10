#pragma once

// Draw order and depth cue for the 3D stage view. Plain C++, no JUCE.

#include <cstddef>
#include <span>
#include <vector>

#include "core/Position.h"
#include "ui/logic/StageTransform.h"

namespace spacer {

inline constexpr float kFarScale = 0.75f;

/// Indices into `positions`, farthest from the viewer first. Equal depths keep
/// their input order. Positions with a non-finite coordinate are left out.
std::vector<std::size_t> farToNear(std::span<const Cartesian> positions, const StageTransform& transform);

/// Size/brightness factor: 1 at depth +extent, kFarScale at depth -extent, linear between, clamped.
float depthCue(const Cartesian& c, const StageTransform& transform);

}  // namespace spacer
