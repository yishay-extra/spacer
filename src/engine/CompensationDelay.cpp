#include "engine/CompensationDelay.h"

#include <algorithm>
#include <cmath>

namespace spacer {

void CompensationDelay::prepare(double sampleRate, int maxBlockSize, int numChannels, int capacity) {
    numChannels_ = std::max(1, numChannels);
    capacity_ = std::max(0, capacity);
    xfadeLength_ = std::max(1, static_cast<int>(std::lround(kCrossfadeMs * 0.001 * sampleRate)));
    if (capacity_ > 0) {
        line_.prepare({sampleRate, static_cast<juce::uint32>(std::max(1, maxBlockSize)),
                       static_cast<juce::uint32>(numChannels_)});
        line_.setMaximumDelayInSamples(capacity_ + 1);
    }
    reset(0);
}

void CompensationDelay::reset(int samples) {
    current_ = target_ = from_ = std::clamp(samples, 0, capacity_);
    xfadePos_ = -1;
    if (capacity_ > 0) line_.reset();
}

void CompensationDelay::setDelay(int samples) noexcept {
    target_ = std::clamp(samples, 0, capacity_);
}

void CompensationDelay::process(float* const* ch, int numChannels, int n) noexcept {
    if (capacity_ == 0) return;
    numChannels = std::min(numChannels, numChannels_);
    for (int i = 0; i < n; ++i) {
        if (xfadePos_ < 0 && target_ != current_) {
            from_ = current_;
            current_ = target_;
            xfadePos_ = 0;
        }
        const bool fading = xfadePos_ >= 0;
        const float t = fading ? static_cast<float>(xfadePos_) / static_cast<float>(xfadeLength_) : 1.0f;
        for (int c = 0; c < numChannels; ++c) {
            line_.pushSample(c, ch[c][i]);
            if (fading) {
                const float a = line_.popSample(c, static_cast<float>(from_), false);
                const float b = line_.popSample(c, static_cast<float>(current_), true);
                ch[c][i] = a + (b - a) * t;
            } else {
                ch[c][i] = line_.popSample(c, static_cast<float>(current_), true);
            }
        }
        if (fading && ++xfadePos_ >= xfadeLength_) xfadePos_ = -1;
    }
}

}  // namespace spacer
