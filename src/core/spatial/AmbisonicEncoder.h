#pragma once

#include <span>
#include <string_view>

#include "core/Layout.h"
#include "core/Spatializer.h"
#include "core/spatial/SphericalHarmonics.h"

namespace spacer {

class AmbisonicDecoder;

/// Ambisonic encoder for one source (SPEC §6.3): order N in [1, 5], ACN
/// channel order, SN3D, real spherical harmonics, direction only (distance is
/// the distance model's job, §6.5).
///
/// render() adds into `bus.ambisonic`; it does nothing when that is null.
/// If the bus order differs from the encoder order, only the channels both
/// have are written.
///
/// **Spread** lowers the effective order: N_eff = N·(1 − spread). Degrees
/// l <= floor(N_eff) get weight 1, degree floor(N_eff)+1 gets the fractional
/// part, higher degrees 0, so spread = 1 is omnidirectional (W only) and the
/// weighting is continuous in spread. The weights are then scaled so that the
/// mean decoded energy does not change with spread — the ambisonic
/// counterpart of Σg² = 1 for VBAP/DBAP. The energy model is the AllRAD
/// decoder for the layout given to prepare() (its `degreeEnergy`, at this
/// encoder's order and max-rE setting); before prepare() it is the ideal
/// uniform-layout model E(l, l') = δ_ll'·(2l+1)·a_l².
class AmbisonicEncoder final : public Spatializer {
public:
    /// `maxRe` must match the project decoder's setting (it only affects the
    /// spread energy compensation).
    explicit AmbisonicEncoder(int order = 3, bool maxRe = true);

    std::string_view name() const override { return "ambisonic"; }

    int order() const { return order_; }
    int numChannels() const { return ambisonicChannels(order_); }

    /// Non-RT. Builds the layout's decoder once to learn its per-degree
    /// energy (spread compensation, see class comment). Does nothing after
    /// useDecoder().
    void prepare(const Layout& layout, double sampleRate, int maxBlockSize) override;

    /// RT. Encoding coefficients at `start` and `end`, linearly interpolated
    /// per sample.
    void render(const float* mono, int numSamples, const Position& start, const Position& end, float spread,
                SpeakerBus& bus) override;

    /// Ambisonics has no per-speaker gains: fills `outGains` with 0.
    void gainsFor(const Position& pos, float spread, std::span<float> outGains) const override;

    /// Non-RT. Takes the spread-compensation energy from an already prepared
    /// project decoder (which must have this encoder's order and max-rE
    /// setting) instead of building one per encoder in prepare(): the engine
    /// builds one decoder per graph and shares it with all of its encoders.
    void useDecoder(const AmbisonicDecoder& decoder);

    /// The encoding coefficients for a direction (numChannels() values, or
    /// fewer if `out` is shorter). RT-safe.
    void coefficientsFor(const Position& pos, float spread, std::span<float> out) const;

    /// Per-degree spread weights w_0..w_N, energy-compensated (see class
    /// comment). Writes order()+1 values. RT-safe.
    void spreadWeights(float spread, float* out) const;

private:
    void resetEnergyModel();  // the uniform-layout model

    int order_;
    bool maxRe_;
    bool energyFromDecoder_{false};
    double energy_[kMaxAmbisonicOrder + 1][kMaxAmbisonicOrder + 1]{};  // E(l, l')
};

}  // namespace spacer
