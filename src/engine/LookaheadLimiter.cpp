#include "engine/LookaheadLimiter.h"

#include <algorithm>
#include <cmath>

namespace spacer {

void LookaheadLimiter::prepare(double sampleRate) {
    lookahead_ = std::max(1, static_cast<int>(std::lround(kLookaheadMs * 0.001 * sampleRate)));
    window_ = lookahead_ + 1;
    ceiling_ = juce::Decibels::decibelsToGain(kCeilingDb);
    releaseCoeff_ = std::exp(-1.0 / (kReleaseMs * 0.001 * sampleRate));

    delay_.prepare({sampleRate, 1, 1});
    delay_.setMaximumDelayInSamples(lookahead_ + 1);
    delay_.setDelay(static_cast<float>(lookahead_));

    dequeValue_.assign(static_cast<std::size_t>(window_ + 1), 1.0f);
    dequeIndex_.assign(static_cast<std::size_t>(window_ + 1), 0);
    avgRing_.assign(static_cast<std::size_t>(window_), 1.0);
    reset();
}

void LookaheadLimiter::reset() {
    delay_.reset();
    dequeHead_ = 0;
    dequeSize_ = 0;
    n_ = 0;
    std::fill(avgRing_.begin(), avgRing_.end(), 1.0);
    avgPos_ = 0;
    avgSum_ = static_cast<double>(window_);
    envelope_ = 1.0;
    minGain_ = 1.0f;
}

void LookaheadLimiter::process(float* x, int numSamples) noexcept {
    const int ringSize = window_ + 1;
    const double invWindow = 1.0 / static_cast<double>(window_);
    float minGain = minGain_;

    for (int i = 0; i < numSamples; ++i) {
        const float in = x[i];
        const float mag = std::abs(in);
        const float required = mag > ceiling_ ? ceiling_ / mag : 1.0f;

        // Sliding minimum of `required` over the last window_ samples.
        while (dequeSize_ > 0) {
            const int back = (dequeHead_ + dequeSize_ - 1) % ringSize;
            if (dequeValue_[static_cast<std::size_t>(back)] < required) break;
            --dequeSize_;
        }
        const int slot = (dequeHead_ + dequeSize_) % ringSize;
        dequeValue_[static_cast<std::size_t>(slot)] = required;
        dequeIndex_[static_cast<std::size_t>(slot)] = n_;
        ++dequeSize_;
        if (dequeIndex_[static_cast<std::size_t>(dequeHead_)] <= n_ - window_) {
            dequeHead_ = (dequeHead_ + 1) % ringSize;
            --dequeSize_;
        }
        const double hold = dequeValue_[static_cast<std::size_t>(dequeHead_)];
        ++n_;

        // Instant attack, exponential release; never above `hold`.
        envelope_ = hold < envelope_ ? hold : hold + (envelope_ - hold) * releaseCoeff_;

        // Moving average over the window.
        auto& oldest = avgRing_[static_cast<std::size_t>(avgPos_)];
        avgSum_ += envelope_ - oldest;
        oldest = envelope_;
        if (++avgPos_ == window_) avgPos_ = 0;
        const float gain = std::min(1.0f, static_cast<float>(avgSum_ * invWindow));

        delay_.pushSample(0, in);
        const float delayed = delay_.popSample(0);
        x[i] = std::clamp(delayed * gain, -ceiling_, ceiling_);
        minGain = std::min(minGain, gain);
    }
    minGain_ = minGain;

    // Re-sum occasionally so the running sum cannot drift over long sessions.
    if ((n_ & 0xFFFF) < numSamples) {
        double sum = 0.0;
        for (double v : avgRing_) sum += v;
        avgSum_ = sum;
    }
}

}  // namespace spacer
