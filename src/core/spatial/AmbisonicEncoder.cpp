#include "core/spatial/AmbisonicEncoder.h"

#include <algorithm>
#include <cmath>
#include <iterator>

#include "core/spatial/AmbisonicDecoder.h"

namespace spacer {

AmbisonicEncoder::AmbisonicEncoder(int order, bool maxRe)
    : order_(std::clamp(order, kMinAmbisonicOrder, kMaxAmbisonicOrder)), maxRe_(maxRe) {
    resetEnergyModel();
}

void AmbisonicEncoder::resetEnergyModel() {
    for (auto& row : energy_) std::fill(std::begin(row), std::end(row), 0.0);
    double a[kMaxAmbisonicOrder + 1];
    if (maxRe_)
        maxReWeights(order_, a);
    else
        std::fill(std::begin(a), std::end(a), 1.0);
    for (int l = 0; l <= order_; ++l) energy_[l][l] = (2.0 * l + 1.0) * a[l] * a[l];
}

void AmbisonicEncoder::prepare(const Layout& layout, double /*sampleRate*/, int /*maxBlockSize*/) {
    if (energyFromDecoder_) return;
    AmbisonicDecoder dec;
    dec.prepare(layout, order_, maxRe_);
    useDecoder(dec);
    energyFromDecoder_ = false;  // prepare() stays re-runnable for another layout
}

void AmbisonicEncoder::useDecoder(const AmbisonicDecoder& dec) {
    resetEnergyModel();
    energyFromDecoder_ = true;
    double total = 0.0;
    for (int l = 0; l <= order_; ++l)
        for (int lp = 0; lp <= order_; ++lp) total += dec.degreeEnergy(l, lp);
    if (!(total > 0.0) || !(dec.degreeEnergy(0, 0) > 0.0)) return;  // no panned speaker: keep the model
    for (int l = 0; l <= kMaxAmbisonicOrder; ++l)                   // zero above the decoder order
        for (int lp = 0; lp <= kMaxAmbisonicOrder; ++lp) energy_[l][lp] = dec.degreeEnergy(l, lp);
}

void AmbisonicEncoder::spreadWeights(float spread, float* out) const {
    const double eff =
        static_cast<double>(order_) * (1.0 - std::clamp(static_cast<double>(spread), 0.0, 1.0));
    const int full = static_cast<int>(std::floor(eff));
    const double frac = eff - full;
    double w[kMaxAmbisonicOrder + 1];
    for (int l = 0; l <= order_; ++l) w[l] = l <= full ? 1.0 : (l == full + 1 ? frac : 0.0);
    double eFull = 0.0, eSpread = 0.0;
    for (int l = 0; l <= order_; ++l)
        for (int lp = 0; lp <= order_; ++lp) {
            eFull += energy_[l][lp];
            eSpread += w[l] * w[lp] * energy_[l][lp];
        }
    const double scale = eSpread > 0.0 && eFull > 0.0 ? std::sqrt(eFull / eSpread) : 1.0;
    for (int l = 0; l <= order_; ++l) out[l] = static_cast<float>(w[l] * scale);
}

void AmbisonicEncoder::coefficientsFor(const Position& pos, float spread, std::span<float> out) const {
    float sh[kMaxAmbisonicChannels];
    float w[kMaxAmbisonicOrder + 1];
    sphericalHarmonicsSN3D(order_, pos.direction(), sh);
    spreadWeights(spread, w);
    const auto n = std::min<std::size_t>(out.size(), static_cast<std::size_t>(numChannels()));
    for (int l = 0, ch = 0; l <= order_; ++l)
        for (int m = -l; m <= l; ++m, ++ch)
            if (static_cast<std::size_t>(ch) < n) out[static_cast<std::size_t>(ch)] = sh[ch] * w[l];
}

void AmbisonicEncoder::gainsFor(const Position& /*pos*/, float /*spread*/, std::span<float> outGains) const {
    std::fill(outGains.begin(), outGains.end(), 0.0f);
}

void AmbisonicEncoder::render(const float* mono, int numSamples, const Position& start, const Position& end,
                              float spread, SpeakerBus& bus) {
    if (numSamples <= 0 || bus.ambisonic == nullptr) return;
    const int channels = std::min(numChannels(), ambisonicChannels(std::max(bus.ambisonicOrder, 0)));
    float c0[kMaxAmbisonicChannels], c1[kMaxAmbisonicChannels];
    coefficientsFor(start, spread, c0);
    coefficientsFor(end, spread, c1);

    const float invN = 1.0f / static_cast<float>(numSamples);
    for (int ch = 0; ch < channels; ++ch) {
        if (c0[ch] == 0.0f && c1[ch] == 0.0f) continue;
        float* out = bus.ambisonic[ch];
        const float step = (c1[ch] - c0[ch]) * invN;
        float g = c0[ch];
        for (int i = 0; i < numSamples; ++i) {
            out[i] += mono[i] * g;
            g += step;
        }
    }
}

}  // namespace spacer
