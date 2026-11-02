#include "core/spatial/Dbap.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace spacer {

namespace {
constexpr float kBlurBaseM = 0.2f;  // r_s at spread 0
constexpr float kBlurSpanM = 3.0f;  // added to r_s at spread 1
}  // namespace

Dbap::Dbap(float rolloffDb) : rolloffDb_(rolloffDb) {}

float Dbap::blurFor(float spread) {
    return kBlurBaseM + std::clamp(spread, 0.0f, 1.0f) * kBlurSpanM;
}

void Dbap::prepare(const Layout& layout, double /*sampleRate*/, int /*maxBlockSize*/) {
    targets_.clear();
    targets_.reserve(layout.speakers.size());
    for (const auto& speaker : layout.speakers)
        targets_.push_back({speaker.position.toCartesian(), speaker.lfe});

    startGains_.assign(targets_.size(), 0.0f);
    endGains_.assign(targets_.size(), 0.0f);
}

void Dbap::computeGains(const Position& pos, float spread, std::span<float> out) const {
    const std::size_t n = std::min(out.size(), targets_.size());
    std::fill(out.begin(), out.end(), 0.0f);

    const Cartesian p = pos.toCartesian();
    const float blur = blurFor(spread);

    // 1. d_i = |p - s_i| + r_s for every panned speaker. Distances are kept in
    //    `out` as scratch space until step 2.
    float dMin = std::numeric_limits<float>::max();
    for (std::size_t i = 0; i < n; ++i) {
        if (targets_[i].lfe) continue;
        out[i] = (p - targets_[i].position).length() + blur;
        dMin = std::min(dMin, out[i]);
    }
    if (dMin == std::numeric_limits<float>::max()) return;  // nothing to pan to

    // 2. g_i = d_i^(-a/2) (SPEC §6.2). Computed relative to the nearest speaker,
    //    (dMin / d_i)^(a/2) in (0, 1]: identical after normalisation, but cannot
    //    overflow for large rolloffs.
    const double exponent = static_cast<double>(rolloffDb_) * 0.5;
    double sumSquares = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        if (targets_[i].lfe) continue;
        const auto g = static_cast<float>(std::pow(static_cast<double>(dMin / out[i]), exponent));
        out[i] = g;
        sumSquares += static_cast<double>(g) * static_cast<double>(g);
    }

    // 3. Normalise so that Σ g² = 1 over the panned speakers (constant power).
    //    The nearest speaker has g = 1, so sumSquares >= 1 and this is safe.
    const auto scale = static_cast<float>(1.0 / std::sqrt(sumSquares));
    for (std::size_t i = 0; i < n; ++i) {
        if (!targets_[i].lfe) out[i] *= scale;
    }
}

void Dbap::gainsFor(const Position& pos, float spread, std::span<float> outGains) const {
    computeGains(pos, spread, outGains);
}

void Dbap::render(const float* mono, int numSamples, const Position& start, const Position& end, float spread,
                  SpeakerBus& bus) {
    if (mono == nullptr || numSamples <= 0 || bus.speakers == nullptr) return;

    computeGains(start, spread, startGains_);
    computeGains(end, spread, endGains_);

    const std::size_t n = std::min(targets_.size(), static_cast<std::size_t>(std::max(bus.numSpeakers, 0)));
    const float invLength = 1.0f / static_cast<float>(numSamples);

    for (std::size_t i = 0; i < n; ++i) {
        if (targets_[i].lfe) continue;  // SPEC §7: LFE is written only by Direct routing
        float* out = bus.speakers[i];
        if (out == nullptr) continue;

        const float g0 = startGains_[i];
        const float g1 = endGains_[i];
        if (g0 == 0.0f && g1 == 0.0f) continue;

        // Sample s reaches the end gain on the last sample of the block.
        const float slope = (g1 - g0) * invLength;
        for (int s = 0; s < numSamples; ++s) {
            const float gain = g0 + slope * static_cast<float>(s + 1);
            out[s] += mono[s] * gain;
        }
    }
}

}  // namespace spacer
