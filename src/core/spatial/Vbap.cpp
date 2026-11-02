#include "core/spatial/Vbap.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <vector>

#include "core/spatial/SphereHull.h"

namespace spacer {

namespace {
constexpr float kTwoPi = 2.0f * kPi;
constexpr float kMinGap = degToRad(0.001f);        // pairs closer than this are coincident
constexpr float kCrossfadeGap = degToRad(179.9f);  // pairs this wide cannot use a 2x2 solve
constexpr float kIs3DElevationDeg = 5.0f;
// The hull "encloses the origin" only if every face plane is at least this far
// from it; nearer faces (e.g. a horizontal ring with nothing below) span almost
// a hemisphere and would pan through arbitrary speaker triples.
const double kMinFaceDistance = std::sin(static_cast<double>(degToRad(kIs3DElevationDeg)));
constexpr float kTriangleEps = 1e-5f;    // g >= -eps counts as inside a triangle
constexpr float kMinRealEnergy = 1e-8f;  // below: source sits on an imaginary speaker

float wrapRad(float a) {
    a = std::fmod(a + kPi, kTwoPi);
    if (a < 0.0f) a += kTwoPi;
    return a - kPi;
}
}  // namespace

void Vbap::prepare(const Layout& layout, double /*sampleRate*/, int /*maxBlockSize*/) {
    numLayoutSpeakers_ = std::min<int>(static_cast<int>(layout.speakers.size()), kMaxSpeakers);
    ringSize_ = 0;
    numPairs_ = 0;
    is3D_ = false;
    numHull_ = numHullReal_ = numTris_ = 0;
    bool elevated = false;

    std::array<int, kMaxSpeakers> order{};
    int n = 0;
    for (int i = 0; i < numLayoutSpeakers_; ++i) {
        const Speaker& s = layout.speakers[static_cast<std::size_t>(i)];
        if (s.lfe) continue;  // SPEC §7: LFE is never panned
        if (std::abs(s.position.elevation) >= kIs3DElevationDeg) elevated = true;
        order[static_cast<std::size_t>(n++)] = i;
    }
    if (elevated) prepare3D(layout, order, n);  // `order` is still in layout order here

    auto az = [&](int layoutIdx) {
        return wrapRad(degToRad(layout.speakers[static_cast<std::size_t>(layoutIdx)].position.azimuth));
    };
    std::stable_sort(order.begin(), order.begin() + n, [&](int x, int y) { return az(x) < az(y); });

    // Drop speakers coincident in azimuth with their predecessor.
    for (int k = 0; k < n; ++k) {
        const float a = az(order[static_cast<std::size_t>(k)]);
        if (ringSize_ > 0 && a - ringAz_[static_cast<std::size_t>(ringSize_ - 1)] < kMinGap) continue;
        ringToLayout_[static_cast<std::size_t>(ringSize_)] = order[static_cast<std::size_t>(k)];
        ringAz_[static_cast<std::size_t>(ringSize_)] = a;
        ++ringSize_;
    }
    // Wrap-around coincidence (-π and +π are the same direction).
    if (ringSize_ > 1 && ringAz_[0] + kTwoPi - ringAz_[static_cast<std::size_t>(ringSize_ - 1)] < kMinGap)
        --ringSize_;

    if (ringSize_ < 2) return;

    for (int k = 0; k < ringSize_; ++k) {
        const int kn = (k + 1) % ringSize_;
        Pair& p = pairs_[static_cast<std::size_t>(numPairs_++)];
        p.a = k;
        p.b = kn;
        p.azA = ringAz_[static_cast<std::size_t>(k)];
        float gap = ringAz_[static_cast<std::size_t>(kn)] - p.azA;
        if (gap <= 0.0f) gap += kTwoPi;
        p.gap = gap;
        p.azB = p.azA + gap;
        p.crossfade = gap >= kCrossfadeGap;
        if (!p.crossfade) {
            // Columns are the unit vectors (x = sin az, y = cos az) of A and B.
            const float ax = std::sin(p.azA), ay = std::cos(p.azA);
            const float bx = std::sin(p.azB), by = std::cos(p.azB);
            const float det = ax * by - bx * ay;  // = sin(gap) > 0
            p.inv[0] = by / det;
            p.inv[1] = -bx / det;
            p.inv[2] = -ay / det;
            p.inv[3] = ax / det;
        }
    }
}

void Vbap::prepare3D(const Layout& layout, const std::array<int, kMaxSpeakers>& panned, int numPanned) {
    // Real speakers in layout order, dropping directions that coincide with an
    // earlier one.
    std::array<Cartesian, kMaxHullPoints> dir{};
    std::array<int, kMaxHullPoints> toLayout{};
    int real = 0;
    for (int k = 0; k < numPanned; ++k) {
        const int li = panned[static_cast<std::size_t>(k)];
        const Cartesian d = layout.speakers[static_cast<std::size_t>(li)].position.direction().normalised();
        bool dup = false;
        for (int j = 0; j < real; ++j)
            if (radToDeg(std::atan2(d.cross(dir[static_cast<std::size_t>(j)]).length(),
                                    d.dot(dir[static_cast<std::size_t>(j)]))) < radToDeg(kMinGap))
                dup = true;
        if (dup) continue;
        dir[static_cast<std::size_t>(real)] = d;
        toLayout[static_cast<std::size_t>(real)] = li;
        ++real;
    }

    // Try: no imaginary speaker, floor, ceiling, both. Take the first that
    // encloses the origin; if none does, the first that spans a volume.
    const Cartesian kFloor{0.0f, 0.0f, -1.0f}, kCeiling{0.0f, 0.0f, 1.0f};
    struct Config {
        int count;
        Cartesian pts[2];
    };
    const Config configs[] = {{0, {}}, {1, {kFloor, {}}}, {1, {kCeiling, {}}}, {2, {kFloor, kCeiling}}};

    SphereHull best;
    int bestConfig = -1;
    std::vector<Cartesian> pts;
    for (int c = 0; c < 4; ++c) {
        pts.assign(dir.begin(), dir.begin() + real);
        for (int k = 0; k < configs[c].count; ++k) {
            const Cartesian& q = configs[c].pts[k];
            bool dup = false;
            for (const Cartesian& d : pts)
                if (d.dot(q) > std::cos(kMinGap)) dup = true;  // a real speaker is already there
            if (!dup) pts.push_back(q);
        }
        if (c > 0 && static_cast<int>(pts.size()) == real) continue;  // nothing added
        SphereHull h = triangulateSphere(pts);
        if (h.degenerate) continue;
        const bool encloses = h.minFaceDistance >= kMinFaceDistance;
        if (bestConfig < 0 || encloses) {
            best = std::move(h);
            bestConfig = c;
            numHull_ = static_cast<int>(pts.size());
            for (std::size_t k = 0; k < pts.size(); ++k) hullDir_[k] = pts[k];
        }
        if (encloses) break;
    }
    if (bestConfig < 0) {  // cannot span a volume even with ±Z: stay 2D
        numHull_ = 0;
        return;
    }

    numHullReal_ = real;
    for (int k = 0; k < numHull_; ++k) {
        hullToLayout_[static_cast<std::size_t>(k)] = k < real ? toLayout[static_cast<std::size_t>(k)] : -1;
        imaginaryNeighbours_[static_cast<std::size_t>(k)] = 0;
    }

    numTris_ = 0;
    for (const auto& t : best.triangles) {
        if (numTris_ >= kMaxTriangles) break;
        const Cartesian& a = hullDir_[static_cast<std::size_t>(t[0])];
        const Cartesian& b = hullDir_[static_cast<std::size_t>(t[1])];
        const Cartesian& c = hullDir_[static_cast<std::size_t>(t[2])];
        // L⁻¹ rows are (b×c, c×a, a×b) / det, det = a·(b×c). Computed in
        // double; a face through the origin (det ≈ 0) cannot pan and is skipped.
        const double ax = a.x, ay = a.y, az = a.z, bx = b.x, by = b.y, bz = b.z, cx = c.x, cy = c.y, cz = c.z;
        const double r0[3] = {by * cz - bz * cy, bz * cx - bx * cz, bx * cy - by * cx};
        const double r1[3] = {cy * az - cz * ay, cz * ax - cx * az, cx * ay - cy * ax};
        const double r2[3] = {ay * bz - az * by, az * bx - ax * bz, ax * by - ay * bx};
        const double det = ax * r0[0] + ay * r0[1] + az * r0[2];
        if (std::abs(det) < 1e-9) continue;
        Triangle& tri = tris_[static_cast<std::size_t>(numTris_++)];
        for (int j = 0; j < 3; ++j) {
            tri.v[j] = t[static_cast<std::size_t>(j)];
            tri.inv[j] = static_cast<float>(r0[j] / det);
            tri.inv[3 + j] = static_cast<float>(r1[j] / det);
            tri.inv[6 + j] = static_cast<float>(r2[j] / det);
        }
        for (int j = 0; j < 3; ++j) {
            if (tri.v[j] < real) continue;
            for (int m = 0; m < 3; ++m)
                if (tri.v[m] < real)
                    imaginaryNeighbours_[static_cast<std::size_t>(tri.v[j])] |= std::uint64_t{1} << tri.v[m];
        }
    }
    is3D_ = numTris_ > 0;
}

std::array<int, 3> Vbap::triangle(int t) const {
    if (!is3D_ || t < 0 || t >= numTris_) return {-1, -1, -1};
    std::array<int, 3> out{};
    for (int j = 0; j < 3; ++j)
        out[static_cast<std::size_t>(j)] =
            hullToLayout_[static_cast<std::size_t>(tris_[static_cast<std::size_t>(t)].v[j])];
    return out;
}

void Vbap::panPoint3D(const Cartesian& p, float* g) const {
    for (int k = 0; k < numHullReal_; ++k) g[k] = 0.0f;

    // Triangle search: the containing triangle has all gains >= 0. Keep the
    // one with the largest minimum gain in case none contains p.
    int best = -1;
    float bestMin = -std::numeric_limits<float>::infinity();
    float bg[3]{};
    for (int t = 0; t < numTris_; ++t) {
        const Triangle& tri = tris_[static_cast<std::size_t>(t)];
        const float g0 = tri.inv[0] * p.x + tri.inv[1] * p.y + tri.inv[2] * p.z;
        const float g1 = tri.inv[3] * p.x + tri.inv[4] * p.y + tri.inv[5] * p.z;
        const float g2 = tri.inv[6] * p.x + tri.inv[7] * p.y + tri.inv[8] * p.z;
        const float m = std::min({g0, g1, g2});
        if (m > bestMin) {
            bestMin = m;
            best = t;
            bg[0] = g0;
            bg[1] = g1;
            bg[2] = g2;
            if (m >= -kTriangleEps) break;
        }
    }

    float energy = 0.0f;
    int imaginary = -1;
    float imaginaryGain = 0.0f;
    if (best >= 0) {
        const Triangle& tri = tris_[static_cast<std::size_t>(best)];
        for (int j = 0; j < 3; ++j) {
            const float gj = std::max(bg[j], 0.0f);
            if (tri.v[j] < numHullReal_) {
                g[tri.v[j]] = gj;
                energy += gj * gj;
            } else if (gj > imaginaryGain) {
                imaginaryGain = gj;
                imaginary = tri.v[j];
            }
        }
    }
    if (energy > kMinRealEnergy) {
        const float norm = 1.0f / std::sqrt(energy);
        for (int k = 0; k < numHullReal_; ++k) g[k] *= norm;
        return;
    }
    for (int k = 0; k < numHullReal_; ++k) g[k] = 0.0f;

    if (imaginary >= 0) {  // at an imaginary speaker: share among its neighbours
        const std::uint64_t nb = imaginaryNeighbours_[static_cast<std::size_t>(imaginary)];
        int count = 0;
        for (int k = 0; k < numHullReal_; ++k)
            if (nb & (std::uint64_t{1} << k)) ++count;
        if (count > 0) {
            const float v = 1.0f / std::sqrt(static_cast<float>(count));
            for (int k = 0; k < numHullReal_; ++k)
                if (nb & (std::uint64_t{1} << k)) g[k] = v;
            return;
        }
    }
    // Uncovered direction: nearest real speaker.
    int nearest = 0;
    float bestDot = -2.0f;
    for (int k = 0; k < numHullReal_; ++k) {
        const float d = hullDir_[static_cast<std::size_t>(k)].dot(p);
        if (d > bestDot) {
            bestDot = d;
            nearest = k;
        }
    }
    if (numHullReal_ > 0) g[nearest] = 1.0f;
}

void Vbap::panPoint(float azRad, float* g) const {
    for (int k = 0; k < ringSize_; ++k) g[k] = 0.0f;
    if (ringSize_ == 0) return;
    if (ringSize_ == 1) {
        g[0] = 1.0f;
        return;
    }
    // Find the pair whose arc [azA, azB] contains the source.
    for (int i = 0; i < numPairs_; ++i) {
        const Pair& p = pairs_[static_cast<std::size_t>(i)];
        float off = azRad - p.azA;
        off -= kTwoPi * std::floor(off / kTwoPi);  // [0, 2π)
        if (off > p.gap && i != numPairs_ - 1) continue;
        // (the last pair absorbs any rounding slop at the seam)
        off = std::min(off, p.gap);
        float ga, gb;
        if (p.crossfade) {
            const float t = off / p.gap * (kPi * 0.5f);
            ga = std::cos(t);
            gb = std::sin(t);
        } else {
            const float px = std::sin(azRad), py = std::cos(azRad);
            ga = p.inv[0] * px + p.inv[1] * py;
            gb = p.inv[2] * px + p.inv[3] * py;
            ga = std::max(ga, 0.0f);
            gb = std::max(gb, 0.0f);
            const float norm = std::sqrt(ga * ga + gb * gb);
            if (norm > 0.0f) {
                ga /= norm;
                gb /= norm;
            }
        }
        g[p.a] = ga;
        g[p.b] = gb;
        return;
    }
}

void Vbap::computeGains(const Position& pos, float spread, float* out) const {
    for (int i = 0; i < numLayoutSpeakers_; ++i) out[i] = 0.0f;
    const float r = std::clamp(spread, 0.0f, 1.0f) * (kPi * 0.5f);
    float sum[kMaxHullPoints]{};
    float g[kMaxHullPoints];

    if (is3D_) {
        // MDAP (SPEC §6.1, 3D): the 8 ring sources lie on the sphere at angular
        // distance r from the direction, evenly spaced in bearing. Bearing 0 is
        // towards increasing azimuth (as in 2D), 90° towards increasing
        // elevation.
        const Cartesian d = pos.direction();
        panPoint3D(d, g);
        for (int k = 0; k < numHullReal_; ++k) sum[k] = g[k];
        if (r > 0.0f) {
            const float az = degToRad(pos.azimuth), el = degToRad(pos.elevation);
            const Cartesian eAz{std::cos(az), -std::sin(az), 0.0f};
            const Cartesian eEl{-std::sin(el) * std::sin(az), -std::sin(el) * std::cos(az), std::cos(el)};
            const float cr = std::cos(r), sr = std::sin(r);
            for (int j = 0; j < kMdapRing; ++j) {
                const float phi = kTwoPi * static_cast<float>(j) / static_cast<float>(kMdapRing);
                const Cartesian q = d * cr + (eAz * std::cos(phi) + eEl * std::sin(phi)) * sr;
                panPoint3D(q, g);
                for (int k = 0; k < numHullReal_; ++k) sum[k] += g[k];
            }
        }
        float energy = 0.0f;
        for (int k = 0; k < numHullReal_; ++k) energy += sum[k] * sum[k];
        const float norm = energy > 0.0f ? 1.0f / std::sqrt(energy) : 0.0f;
        for (int k = 0; k < numHullReal_; ++k)
            out[hullToLayout_[static_cast<std::size_t>(k)]] = sum[k] * norm;
        return;
    }

    if (ringSize_ == 0) return;
    const float az = degToRad(pos.azimuth);

    panPoint(az, g);
    for (int k = 0; k < ringSize_; ++k) sum[k] = g[k];

    // MDAP (SPEC §6.1): 1 + 8 virtual sources on a ring of angular radius
    // spread·90° around the direction. Elevation is ignored in 2D mode, so the
    // ring projects onto the horizontal plane as azimuth offsets r·cos(φ_k).
    if (r > 0.0f) {
        for (int j = 0; j < kMdapRing; ++j) {
            const float phi = kTwoPi * static_cast<float>(j) / static_cast<float>(kMdapRing);
            panPoint(az + r * std::cos(phi), g);
            for (int k = 0; k < ringSize_; ++k) sum[k] += g[k];
        }
    }

    float energy = 0.0f;
    for (int k = 0; k < ringSize_; ++k) energy += sum[k] * sum[k];
    const float norm = energy > 0.0f ? 1.0f / std::sqrt(energy) : 0.0f;
    for (int k = 0; k < ringSize_; ++k) out[ringToLayout_[static_cast<std::size_t>(k)]] = sum[k] * norm;
}

void Vbap::gainsFor(const Position& pos, float spread, std::span<float> outGains) const {
    float tmp[kMaxSpeakers];
    computeGains(pos, spread, tmp);
    const auto n = std::min<std::size_t>(outGains.size(), static_cast<std::size_t>(numLayoutSpeakers_));
    for (std::size_t i = 0; i < n; ++i) outGains[i] = tmp[i];
    for (std::size_t i = n; i < outGains.size(); ++i) outGains[i] = 0.0f;
}

void Vbap::render(const float* mono, int numSamples, const Position& start, const Position& end, float spread,
                  SpeakerBus& bus) {
    if (numSamples <= 0) return;
    float g0[kMaxSpeakers], g1[kMaxSpeakers];
    computeGains(start, spread, g0);
    computeGains(end, spread, g1);

    const int n = std::min(numLayoutSpeakers_, bus.numSpeakers);
    const float invN = 1.0f / static_cast<float>(numSamples);
    for (int s = 0; s < n; ++s) {
        if (g0[s] == 0.0f && g1[s] == 0.0f) continue;
        float* out = bus.speakers[s];
        const float step = (g1[s] - g0[s]) * invN;
        float g = g0[s];
        for (int i = 0; i < numSamples; ++i) {
            out[i] += mono[i] * g;
            g += step;
        }
    }
}

}  // namespace spacer
