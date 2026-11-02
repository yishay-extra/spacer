#include "core/spatial/Vbap.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace spacer {

namespace {
constexpr float kTwoPi = 2.0f * kPi;
constexpr float kMinGap = degToRad(0.001f);        // pairs closer than this are coincident
constexpr float kCrossfadeGap = degToRad(179.9f);  // pairs this wide cannot use a 2x2 solve
constexpr float kIs3DElevationDeg = 5.0f;

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
    is2D_ = true;

    std::array<int, kMaxSpeakers> order{};
    int n = 0;
    for (int i = 0; i < numLayoutSpeakers_; ++i) {
        const Speaker& s = layout.speakers[static_cast<std::size_t>(i)];
        if (s.lfe) continue;  // SPEC §7: LFE is never panned
        if (std::abs(s.position.elevation) >= kIs3DElevationDeg) is2D_ = false;
        order[static_cast<std::size_t>(n++)] = i;
    }

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

void Vbap::computeGains(float azimuthDeg, float spread, float* out) const {
    for (int i = 0; i < numLayoutSpeakers_; ++i) out[i] = 0.0f;
    if (ringSize_ == 0) return;

    float sum[kMaxSpeakers]{};
    float g[kMaxSpeakers];
    const float az = degToRad(azimuthDeg);

    panPoint(az, g);
    for (int k = 0; k < ringSize_; ++k) sum[k] = g[k];

    // MDAP (SPEC §6.1): 1 + 8 virtual sources on a ring of angular radius
    // spread·90° around the direction. Elevation is ignored in 2D mode, so the
    // ring projects onto the horizontal plane as azimuth offsets r·cos(φ_k).
    const float r = std::clamp(spread, 0.0f, 1.0f) * (kPi * 0.5f);
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
    computeGains(pos.azimuth, spread, tmp);
    const auto n = std::min<std::size_t>(outGains.size(), static_cast<std::size_t>(numLayoutSpeakers_));
    for (std::size_t i = 0; i < n; ++i) outGains[i] = tmp[i];
    for (std::size_t i = n; i < outGains.size(); ++i) outGains[i] = 0.0f;
}

void Vbap::render(const float* mono, int numSamples, const Position& start, const Position& end, float spread,
                  SpeakerBus& bus) {
    if (numSamples <= 0) return;
    float g0[kMaxSpeakers], g1[kMaxSpeakers];
    computeGains(start.azimuth, spread, g0);
    computeGains(end.azimuth, spread, g1);

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
