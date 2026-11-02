#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>

namespace spacer {

/// Linear gain ramp (SPEC §5.4) with a per-change ramp time: a thin wrapper
/// over juce::SmoothedValue that lets each new target choose its own ramp
/// length without jumping. Real-time safe.
class GainRamp {
public:
    static constexpr float kDefaultRampMs = 10.0f;

    void prepare(double sampleRate, float initialGain) {
        sampleRate_ = sampleRate;
        value_.reset(stepsFor(kDefaultRampMs));
        value_.setCurrentAndTargetValue(initialGain);
    }

    /// rampMs < 0 → default (10 ms); 0 → jump.
    void setTarget(float gain, float rampMs = -1.0f) noexcept {
        const float current = value_.getCurrentValue();
        value_.reset(stepsFor(rampMs < 0.0f ? kDefaultRampMs : rampMs));
        value_.setCurrentAndTargetValue(current);
        value_.setTargetValue(gain);
    }

    /// Ramp over exactly `numSamples` samples.
    void setTargetInSamples(float gain, int numSamples) noexcept {
        const float current = value_.getCurrentValue();
        value_.reset(numSamples);
        value_.setCurrentAndTargetValue(current);
        value_.setTargetValue(gain);
    }

    void snapTo(float gain) noexcept { value_.setCurrentAndTargetValue(gain); }

    float current() const noexcept { return value_.getCurrentValue(); }
    float target() const noexcept { return value_.getTargetValue(); }
    bool isRamping() const noexcept { return value_.isSmoothing(); }
    /// True when settled at exactly zero: callers may skip the work.
    bool isSilent() const noexcept {
        return !value_.isSmoothing() && juce::exactlyEqual(value_.getTargetValue(), 0.0f);
    }

    float next() noexcept { return value_.getNextValue(); }
    void apply(float* samples, int numSamples) noexcept { value_.applyGain(samples, numSamples); }

private:
    int stepsFor(float ms) const noexcept {
        return static_cast<int>(std::lround(static_cast<double>(ms) * 0.001 * sampleRate_));
    }

    double sampleRate_{48000.0};
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> value_;
};

}  // namespace spacer
