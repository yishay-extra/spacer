#pragma once

#include <array>
#include <span>
#include <string_view>

#include "core/Layout.h"
#include "core/Spatializer.h"

namespace spacer {

/// Vector-base amplitude panning, 2D mode (SPEC §6.1): pairwise panning on the
/// ring of speakers sorted by azimuth. Source elevation is ignored.
///
/// - Speakers with `lfe == true` are never panned and always get gain 0.
/// - Coverage gaps of >= 180 degrees (e.g. behind a stereo pair) cannot be
///   expressed as a VBAP pair; there the two bounding speakers are
///   cross-faded constant-power by angle across the gap, so gains stay
///   continuous and Σg² = 1.
/// - Speakers sharing the same azimuth: only the first (layout order) is used.
/// - 3D layouts (any panned |el| >= 5 deg) are WP2b; until then this class
///   still pans them by azimuth only. `is2D()` tells the caller which it is.
class Vbap final : public Spatializer {
public:
    std::string_view name() const override { return "vbap"; }

    void prepare(const Layout& layout, double sampleRate, int maxBlockSize) override;

    void render(const float* mono, int numSamples, const Position& start, const Position& end, float spread,
                SpeakerBus& bus) override;

    void gainsFor(const Position& pos, float spread, std::span<float> outGains) const override;

    /// True if every panned speaker has |elevation| < 5 degrees.
    bool is2D() const { return is2D_; }
    int numPannedSpeakers() const { return ringSize_; }

private:
    struct Pair {
        float azA{0.0f}, azB{0.0f};  // ring azimuths, radians; azB = azA + gap
        float gap{0.0f};             // radians, (0, 2π]
        float inv[4]{};              // inverse of [a b] (row-major), valid if !crossfade
        bool crossfade{false};
        int a{0}, b{0};  // indices into ring order
    };

    void computeGains(float azimuthDeg, float spread, float* out) const;
    void panPoint(float azRad, float* ringGains) const;

    static constexpr int kMdapRing = 8;

    std::array<int, kMaxSpeakers> ringToLayout_{};  // ring order → layout index
    std::array<float, kMaxSpeakers> ringAz_{};      // radians, ascending in [-π, π)
    std::array<Pair, kMaxSpeakers> pairs_{};
    int ringSize_{0};
    int numPairs_{0};
    int numLayoutSpeakers_{0};
    bool is2D_{true};
};

}  // namespace spacer
