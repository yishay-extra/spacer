#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

#include "core/Layout.h"
#include "core/Spatializer.h"

namespace spacer {

/// Vector-base amplitude panning (SPEC §6.1). The mode is chosen in prepare():
///
/// **2D** when every panned speaker has |elevation| < 5 deg: pairwise panning
/// on the ring of speakers sorted by azimuth. Source elevation is ignored.
/// - Coverage gaps of >= 180 degrees (e.g. behind a stereo pair) cannot be
///   expressed as a VBAP pair; there the two bounding speakers are
///   cross-faded constant-power by angle across the gap, so gains stay
///   continuous and Σg² = 1.
/// - Speakers sharing the same azimuth: only the first (layout order) is used.
///
/// **3D** otherwise: the speaker directions are triangulated (convex hull of
/// unit vectors, `SphereHull`). If the hull does not enclose the origin with a
/// margin (a face plane closer than sin 5° to it), an imaginary speaker is
/// added at -Z (floor); failing that at +Z (ceiling); failing that both.
/// Imaginary gains are discarded and the rest renormalised. The triangle
/// containing the source gives `g = L⁻¹p`; negatives are clamped to 0.
/// - Speakers sharing a direction: only the first (layout order) is used.
/// - A source exactly at an imaginary speaker (where every real gain is 0) is
///   spread equally over the real speakers adjacent to it in the hull.
/// - A direction no triangle covers (only possible when no configuration
///   encloses the origin) uses the least-outside triangle; if that yields no
///   positive gain, the nearest speaker.
/// - If even ±Z cannot make the directions span a volume, falls back to 2D.
///
/// In both modes speakers with `lfe == true` are never panned and always get
/// gain 0 (SPEC §7), and spread uses MDAP (1 + 8 virtual sources, amplitude
/// sum, then Σg² = 1).
class Vbap final : public Spatializer {
public:
    std::string_view name() const override { return "vbap"; }

    /// Use 3D mode even when every panned speaker is horizontal (2D mode is
    /// then used only if the hull stays degenerate with ±Z added). For the
    /// AllRAD decoder (SPEC §6.3: "3D mode, with imaginary speakers at ±Z when
    /// needed"). Takes effect at the next prepare(). Default false.
    void setAlways3D(bool always3D) { always3D_ = always3D; }

    void prepare(const Layout& layout, double sampleRate, int maxBlockSize) override;

    void render(const float* mono, int numSamples, const Position& start, const Position& end, float spread,
                SpeakerBus& bus) override;

    void gainsFor(const Position& pos, float spread, std::span<float> outGains) const override;

    /// True if the 2D mode is in use (every panned speaker has |elevation|
    /// < 5 degrees, or the 3D hull is degenerate).
    bool is2D() const { return !is3D_; }
    /// Distinct real speakers taking part in panning (LFE and duplicates
    /// excluded).
    int numPannedSpeakers() const { return is3D_ ? numHullReal_ : ringSize_; }

    /// 3D mode only: the triangulation. Entries are layout speaker indices, or
    /// -1 for an imaginary speaker.
    int numTriangles() const { return is3D_ ? numTris_ : 0; }
    std::array<int, 3> triangle(int t) const;
    /// 3D mode only: imaginary speakers added (0, 1 or 2).
    int numImaginarySpeakers() const { return is3D_ ? numHull_ - numHullReal_ : 0; }

    static constexpr int kMaxHullPoints = kMaxSpeakers + 2;
    static constexpr int kMaxTriangles = 2 * kMaxHullPoints - 4;  // Euler bound

private:
    struct Pair {
        float azA{0.0f}, azB{0.0f};  // ring azimuths, radians; azB = azA + gap
        float gap{0.0f};             // radians, (0, 2π]
        float inv[4]{};              // inverse of [a b] (row-major), valid if !crossfade
        bool crossfade{false};
        int a{0}, b{0};  // indices into ring order
    };

    struct Triangle {
        int v[3]{};      // hull point indices
        float inv[9]{};  // rows of L⁻¹, L = [v0 v1 v2] as columns
    };

    void prepare3D(const Layout& layout, const std::array<int, kMaxSpeakers>& panned, int numPanned);
    void computeGains(const Position& pos, float spread, float* out) const;
    void panPoint(float azRad, float* ringGains) const;
    void panPoint3D(const Cartesian& p, float* hullRealGains) const;

    static constexpr int kMdapRing = 8;

    std::array<int, kMaxSpeakers> ringToLayout_{};  // ring order → layout index
    std::array<float, kMaxSpeakers> ringAz_{};      // radians, ascending in [-π, π)
    std::array<Pair, kMaxSpeakers> pairs_{};
    int ringSize_{0};
    int numPairs_{0};
    int numLayoutSpeakers_{0};
    bool is3D_{false};
    bool always3D_{false};

    // 3D: hull points are the real speakers [0, numHullReal_) followed by the
    // imaginary ones [numHullReal_, numHull_).
    std::array<int, kMaxHullPoints> hullToLayout_{};
    std::array<Cartesian, kMaxHullPoints> hullDir_{};
    std::array<std::uint64_t, kMaxHullPoints> imaginaryNeighbours_{};  // bitmask of real hull points
    std::array<Triangle, kMaxTriangles> tris_{};
    int numHull_{0};
    int numHullReal_{0};
    int numTris_{0};
};

}  // namespace spacer
