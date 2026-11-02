#include "core/spatial/DistanceModel.h"

#include <algorithm>
#include <cmath>

#include "core/Position.h"

namespace spacer {

namespace {
constexpr float kRRef = 1.0f;
constexpr float kMaxCutoffHz = 20000.0f;
}  // namespace

float distanceGain(const DistanceModel& m, float distance) {
    if (m.k == 0.0f) return 1.0f;
    const float d = std::max(distance, std::max(m.rMin, 1e-6f));
    return std::pow(kRRef / d, m.k);
}

float distanceCutoffHz(const DistanceModel& m, float distance) {
    if (m.q == 0.0f) return kMaxCutoffHz;
    return kMaxCutoffHz * std::pow(kRRef / std::max(distance, kRRef), m.q);
}

void DistanceProcessor::prepare(double sampleRate) {
    sampleRate_ = sampleRate;
    state_ = 0.0f;
}

void DistanceProcessor::reset() {
    state_ = 0.0f;
}

void DistanceProcessor::process(float* x, int n, float d0, float d1, const DistanceModel& m) {
    if (n <= 0) return;
    const float invN = 1.0f / static_cast<float>(n);

    const float g0 = distanceGain(m, d0), g1 = distanceGain(m, d1);
    const float gStep = (g1 - g0) * invN;

    if (m.q == 0.0f) {
        state_ = 0.0f;  // filter off; start clean when it is switched on
        float g = g0;
        for (int i = 0; i < n; ++i) {
            x[i] *= g;
            g += gStep;
        }
        return;
    }

    // one-pole: y += a (x - y), a = 1 - exp(-2π fc / sr)
    const auto coeff = [&](float d) {
        const float fc = std::min(distanceCutoffHz(m, d), static_cast<float>(0.49 * sampleRate_));
        return 1.0f - std::exp(-2.0f * kPi * fc / static_cast<float>(sampleRate_));
    };
    const float a0 = coeff(d0), a1 = coeff(d1);
    const float aStep = (a1 - a0) * invN;
    float g = g0, a = a0, y = state_;
    for (int i = 0; i < n; ++i) {
        y += a * (x[i] - y);
        x[i] = y * g;
        g += gStep;
        a += aStep;
    }
    state_ = y;
}

}  // namespace spacer
