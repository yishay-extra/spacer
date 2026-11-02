#pragma once

#include <array>
#include <span>

#include "core/Layout.h"
#include "core/Spatializer.h"
#include "core/spatial/SphericalHarmonics.h"

namespace spacer {

/// The project's ambisonic decoder (SPEC §6.3): AllRAD (Zotter & Frank 2012).
///
/// prepare() (non-RT, allocates) builds a (speakers × (N+1)²) matrix:
///  1. sample the sphere with the embedded 240-point 21-design (t >= 2N+1 for
///     every N <= 5);
///  2. pan each t-design point to the layout with 3D VBAP (`Vbap` with
///     `setAlways3D`, so imaginary speakers at −Z / +Z are added as needed and
///     their gains discarded); LFE speakers (SPEC §7) are not part of that
///     target layout and get an all-zero row;
///  3. decoder = VBAP gains · sampling decoder · diag(a_l), with a_l the max-rE
///     weights (or 1 when max-rE is off). The sampling decoder for SN3D input
///     is (1/K)·(2l+1)·Y_n(θ_k);
///  4. scale the whole matrix so that the mean decoded energy Σ_s g_s² of a
///     unit plane wave (averaged over the t-design directions) is 1, matching
///     the Σg² = 1 convention of VBAP/DBAP.
///
/// decode() (RT) is the matrix multiply, adding into `bus.speakers`.
class AmbisonicDecoder {
public:
    /// Non-RT. Rebuild for a layout and order (clamped to [1, 5]). Rows are
    /// layout speaker indices (the first kMaxSpeakers).
    void prepare(const Layout& layout, int order, bool maxRe = true);

    /// RT. bus.speakers[s] += Σ_n D[s][n]·bus.ambisonic[n] for the speakers
    /// and channels both the decoder and the bus have. Does nothing if
    /// `bus.ambisonic` is null.
    void decode(SpeakerBus& bus) const;

    int order() const { return order_; }
    int numChannels() const { return ambisonicChannels(order_); }
    int numSpeakers() const { return numSpeakers_; }
    bool isPrepared() const { return prepared_; }

    /// Decode-matrix entry for layout speaker `speaker`, ACN channel `acn`.
    float coefficient(int speaker, int acn) const {
        return matrix_[static_cast<std::size_t>(speaker * kMaxAmbisonicChannels + acn)];
    }

    /// Mean decoded energy per pair of degrees: E(l, l') = mean over the
    /// t-design directions θ of Σ_s (D_l·Y_l(θ))_s·(D_l'·Y_l'(θ))_s, where D_l
    /// is the part of the matrix for degree l. A plane wave encoded with
    /// per-degree weights w decodes with mean energy Σ w_l·w_l'·E(l, l');
    /// E summed over all pairs is 1. Used by the encoder's spread
    /// compensation.
    double degreeEnergy(int l, int lp) const {
        return degreeEnergy_[static_cast<std::size_t>(l * (kMaxAmbisonicOrder + 1) + lp)];
    }

    /// Non-RT, for tests and UI: speaker gains for one encoded ambisonic frame
    /// (`ambi` holds numChannels() values).
    void decodeFrame(std::span<const float> ambi, std::span<float> outGains) const;

private:
    std::array<float, kMaxSpeakers * kMaxAmbisonicChannels> matrix_{};
    std::array<double, (kMaxAmbisonicOrder + 1) * (kMaxAmbisonicOrder + 1)> degreeEnergy_{};
    int order_{1};
    int numSpeakers_{0};
    bool prepared_{false};
};

}  // namespace spacer
