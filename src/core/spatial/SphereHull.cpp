#include "core/spatial/SphereHull.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace spacer {

namespace {

struct V3 {
    double x, y, z;
};

V3 sub(const V3& a, const V3& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
double dot(const V3& a, const V3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
V3 cross(const V3& a, const V3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
V3 scale(const V3& a, double s) {
    return {a.x * s, a.y * s, a.z * s};
}
double length(const V3& a) {
    return std::sqrt(dot(a, a));
}

// Points closer than this to a plane count as lying on it. Speaker angles are
// floats, so symmetric layouts are coplanar only to ~1e-7.
constexpr double kPlaneEps = 1e-5;

}  // namespace

SphereHull triangulateSphere(std::span<const Cartesian> points) {
    SphereHull hull;
    const int n = static_cast<int>(points.size());
    if (n < 4 || n > 64) return hull;

    std::vector<V3> p(points.size());
    for (std::size_t i = 0; i < points.size(); ++i) p[i] = {points[i].x, points[i].y, points[i].z};

    std::vector<std::uint64_t> seen;
    hull.minFaceDistance = std::numeric_limits<double>::infinity();

    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j)
            for (int k = j + 1; k < n; ++k) {
                V3 nrm = cross(sub(p[j], p[i]), sub(p[k], p[i]));
                const double len = length(nrm);
                if (len < 1e-12) continue;
                nrm = scale(nrm, 1.0 / len);
                double d = dot(nrm, p[i]);

                int above = 0, below = 0;
                std::uint64_t mask = 0;
                for (int m = 0; m < n; ++m) {
                    const double s = dot(nrm, p[m]) - d;
                    if (s > kPlaneEps)
                        ++above;
                    else if (s < -kPlaneEps)
                        ++below;
                    else
                        mask |= std::uint64_t{1} << m;
                }
                if (above == 0 && below == 0) return SphereHull{};  // all coplanar
                if (above > 0 && below > 0) continue;               // not a supporting plane
                if (above > 0) {                                    // make the normal point outwards
                    nrm = scale(nrm, -1.0);
                    d = -d;
                }
                if (std::find(seen.begin(), seen.end(), mask) != seen.end()) continue;
                seen.push_back(mask);
                hull.minFaceDistance = std::min(hull.minFaceDistance, d);

                std::vector<int> face;
                for (int m = 0; m < n; ++m)
                    if (mask & (std::uint64_t{1} << m)) face.push_back(m);
                if (face.size() == 3) {
                    hull.triangles.push_back({face[0], face[1], face[2]});
                    continue;
                }

                // Coplanar polygon: all its points lie on one circle, so each is
                // a vertex. Sort by angle around the centroid, then fan from the
                // lowest index (face[0]).
                V3 c{0.0, 0.0, 0.0};
                for (int m : face) c = {c.x + p[m].x, c.y + p[m].y, c.z + p[m].z};
                c = scale(c, 1.0 / static_cast<double>(face.size()));
                const V3 u0 = sub(p[face[0]], c);
                const V3 u = scale(u0, 1.0 / length(u0));
                const V3 w = cross(nrm, u);
                auto angle = [&](int m) {
                    const V3 q = sub(p[m], c);
                    const double a = std::atan2(dot(q, w), dot(q, u));
                    return a < 0.0 ? a + 2.0 * std::numbers::pi : a;
                };
                std::vector<int> ring(face.begin() + 1, face.end());
                std::stable_sort(ring.begin(), ring.end(), [&](int a, int b) { return angle(a) < angle(b); });
                for (std::size_t t = 0; t + 1 < ring.size(); ++t)
                    hull.triangles.push_back({face[0], ring[t], ring[t + 1]});
            }

    hull.degenerate = hull.triangles.empty();
    if (hull.degenerate) hull.minFaceDistance = 0.0;
    return hull;
}

}  // namespace spacer
