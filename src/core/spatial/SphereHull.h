#pragma once

#include <array>
#include <span>
#include <vector>

#include "core/Position.h"

namespace spacer {

/// Convex hull of a set of distinct unit vectors, triangulated (used by 3D
/// VBAP, SPEC §6.1). Non-RT: allocates.
///
/// Because every point lies on the unit sphere, no three points are collinear
/// and every point is a hull vertex. Faces with more than three coplanar
/// points (e.g. the four top speakers of 7.1.4) are split into a fan from the
/// vertex with the lowest index, so the result is deterministic for a given
/// input order.
struct SphereHull {
    std::vector<std::array<int, 3>> triangles;  // indices into the input points
    /// True if the points do not span a volume (fewer than 4, or all coplanar).
    bool degenerate{true};
    /// Smallest distance from the origin to a face plane, measured along the
    /// outward normal. <= 0 means the origin is outside or on the hull.
    double minFaceDistance{0.0};
};

/// Build the hull. `points` must be unit length and pairwise distinct.
/// Exhaustive face test, O(n^4); fine for n <= kMaxSpeakers + 2.
SphereHull triangulateSphere(std::span<const Cartesian> points);

}  // namespace spacer
