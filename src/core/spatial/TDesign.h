#pragma once

#include <array>
#include <span>

namespace spacer {

/// The spherical t-design used by the AllRAD decoder (SPEC §6.3): a 21-design
/// with 240 points, so t >= 2N+1 holds for every supported order (N <= 5).
/// Unit vectors (x, y, z) in Spacer coordinates. A t-design integrates every
/// polynomial of degree <= t on the sphere exactly by the mean over its
/// points.
std::span<const std::array<double, 3>> tDesign21();

inline constexpr int kTDesign21Degree = 21;

}  // namespace spacer
