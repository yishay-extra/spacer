#include "core/spatial/AmbisonicDecoder.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "core/spatial/TDesign.h"
#include "core/spatial/Vbap.h"

namespace spacer {

void AmbisonicDecoder::prepare(const Layout& layout, int order, bool maxRe) {
    order_ = std::clamp(order, kMinAmbisonicOrder, kMaxAmbisonicOrder);
    numSpeakers_ = std::min<int>(static_cast<int>(layout.speakers.size()), kMaxSpeakers);
    matrix_.fill(0.0f);
    prepared_ = true;

    const int channels = numChannels();
    const auto design = tDesign21();
    const int numPoints = static_cast<int>(design.size());
    const auto S = static_cast<std::size_t>(numSpeakers_);
    const auto C = static_cast<std::size_t>(channels);

    double weight[kMaxAmbisonicOrder + 1];
    if (maxRe)
        maxReWeights(order_, weight);
    else
        std::fill(std::begin(weight), std::end(weight), 1.0);

    // VBAP from every t-design point to the layout (LFE gets 0 from Vbap).
    Vbap vbap;
    vbap.setAlways3D(true);
    vbap.prepare(layout, 48000.0, 1);

    std::vector<double> sh(static_cast<std::size_t>(numPoints) * C);  // Y_n(θ_k), SN3D
    std::vector<double> d(S * C, 0.0);                                // decoder, double
    std::vector<float> gains(S);
    float y[kMaxAmbisonicChannels];
    for (int k = 0; k < numPoints; ++k) {
        const auto& p = design[static_cast<std::size_t>(k)];
        const Cartesian dir{static_cast<float>(p[0]), static_cast<float>(p[1]), static_cast<float>(p[2])};
        sphericalHarmonicsSN3D(order_, dir, y);
        for (std::size_t n = 0; n < C; ++n) sh[static_cast<std::size_t>(k) * C + n] = y[n];
        vbap.gainsFor(Position::fromCartesian(dir), 0.0f, gains);
        for (std::size_t s = 0; s < S; ++s) {
            if (gains[s] == 0.0f) continue;
            for (int l = 0, n = 0; l <= order_; ++l)
                for (int m = -l; m <= l; ++m, ++n)
                    d[s * C + static_cast<std::size_t>(n)] +=
                        gains[s] * (2.0 * l + 1.0) * weight[l] * y[n] / numPoints;
        }
    }

    // Per-degree energy, then normalise: mean over the evaluation directions
    // of Σ_s (D·Y(θ_k))_s² = Σ_{l,l'} E(l, l') = 1. Those directions are the
    // t-design (the sphere), or for a horizontal layout the horizontal plane,
    // where its sources live: there the sphere mean leaves an in-plane source
    // ≈ +0.7..1.1 dB above the Σg² = 1 of VBAP/DBAP (WP3b note, WP14).
    horizontal_ = isHorizontalLayout(layout);
    std::vector<double> ring;
    int evalPoints = numPoints;
    const double* evalSh = sh.data();
    if (horizontal_) {
        evalPoints = kHorizontalPoints;
        ring.resize(static_cast<std::size_t>(evalPoints) * C);
        for (int k = 0; k < evalPoints; ++k) {
            const auto az = static_cast<float>(360.0 * k / evalPoints);
            sphericalHarmonicsSN3D(order_, Position{az, 0.0f, 1.0f}.toCartesian(), y);
            for (std::size_t n = 0; n < C; ++n) ring[static_cast<std::size_t>(k) * C + n] = y[n];
        }
        evalSh = ring.data();
    }
    const int degrees = order_ + 1;
    std::vector<double> e(static_cast<std::size_t>(degrees * degrees), 0.0);
    std::vector<double> part(static_cast<std::size_t>(degrees));
    for (int k = 0; k < evalPoints; ++k) {
        const double* yk = &evalSh[static_cast<std::size_t>(k) * C];
        for (std::size_t s = 0; s < S; ++s) {
            for (int l = 0; l < degrees; ++l) {
                double g = 0.0;
                for (int n = l * l; n < (l + 1) * (l + 1); ++n)
                    g += d[s * C + static_cast<std::size_t>(n)] * yk[n];
                part[static_cast<std::size_t>(l)] = g;
            }
            for (int l = 0; l < degrees; ++l)
                for (int lp = 0; lp < degrees; ++lp)
                    e[static_cast<std::size_t>(l * degrees + lp)] +=
                        part[static_cast<std::size_t>(l)] * part[static_cast<std::size_t>(lp)];
        }
    }
    double energy = 0.0;
    for (double v : e) energy += v / evalPoints;
    const double scale = energy > 0.0 ? 1.0 / std::sqrt(energy) : 0.0;
    for (std::size_t s = 0; s < S; ++s)
        for (std::size_t n = 0; n < C; ++n)
            matrix_[s * kMaxAmbisonicChannels + n] = static_cast<float>(d[s * C + n] * scale);
    degreeEnergy_.fill(0.0);
    for (int l = 0; l < degrees; ++l)
        for (int lp = 0; lp < degrees; ++lp)
            degreeEnergy_[static_cast<std::size_t>(l * (kMaxAmbisonicOrder + 1) + lp)] =
                e[static_cast<std::size_t>(l * degrees + lp)] / evalPoints * scale * scale;
}

bool AmbisonicDecoder::isHorizontalLayout(const Layout& layout) {
    bool any = false;
    for (const auto& sp : layout.speakers) {
        if (sp.lfe) continue;
        any = true;
        if (std::abs(sp.position.elevation) >= kHorizontalElevationDeg) return false;
    }
    return any;
}

void AmbisonicDecoder::decode(SpeakerBus& bus) const {
    if (!prepared_ || bus.ambisonic == nullptr || bus.speakers == nullptr || bus.numSamples <= 0) return;
    const int channels = std::min(numChannels(), ambisonicChannels(std::max(bus.ambisonicOrder, 0)));
    const int speakers = std::min(numSpeakers_, bus.numSpeakers);
    const int n = bus.numSamples;
    for (int s = 0; s < speakers; ++s) {
        float* out = bus.speakers[s];
        const float* row = &matrix_[static_cast<std::size_t>(s * kMaxAmbisonicChannels)];
        for (int ch = 0; ch < channels; ++ch) {
            const float c = row[ch];
            if (c == 0.0f) continue;
            const float* in = bus.ambisonic[ch];
            for (int i = 0; i < n; ++i) out[i] += c * in[i];
        }
    }
}

void AmbisonicDecoder::decodeFrame(std::span<const float> ambi, std::span<float> outGains) const {
    const auto channels = std::min<std::size_t>(ambi.size(), static_cast<std::size_t>(numChannels()));
    for (std::size_t s = 0; s < outGains.size(); ++s) {
        double g = 0.0;
        if (s < static_cast<std::size_t>(numSpeakers_))
            for (std::size_t n = 0; n < channels; ++n)
                g += coefficient(static_cast<int>(s), static_cast<int>(n)) * ambi[n];
        outGains[s] = static_cast<float>(g);
    }
}

}  // namespace spacer
