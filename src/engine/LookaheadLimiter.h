#pragma once

#include <juce_dsp/juce_dsp.h>

#include <vector>

namespace spacer {

/// Per-speaker output limiter (SPEC §5.5): look-ahead peak limiter, ceiling
/// −0.3 dBFS, look-ahead 1.5 ms, release 80 ms, no make-up gain.
///
/// juce::dsp::Limiter has no look-ahead and lets peaks through to its 0 dBFS
/// clipper, so it cannot meet the ceiling. The audio delay is a
/// juce::dsp::DelayLine; the gain computer is the standard
/// "min-hold → release → moving average" design, which guarantees the gain
/// applied to every sample is ≤ the gain that sample needs:
///
///   r[n] = min(1, c/|x[n]|)            required gain
///   h[n] = min r[n-W+1 .. n]           hold over the window W = L+1
///   e[n] = h[n] if h[n] < e[n-1], else release towards h[n] (e ≤ h)
///   s[n] = mean e[n-W+1 .. n]          smooth attack ramp
///   y[n] = x[n-L] · s[n]               every term of the mean is ≤ r[n-L]
///
/// A final clamp to the ceiling only catches float rounding. Latency is
/// exactly L samples. prepare() allocates; process() does not.
class LookaheadLimiter {
public:
    static constexpr float kCeilingDb = -0.3f;
    static constexpr float kLookaheadMs = 1.5f;
    static constexpr float kReleaseMs = 80.0f;

    void prepare(double sampleRate);
    void reset();

    /// In place.
    void process(float* samples, int numSamples) noexcept;

    int latencySamples() const noexcept { return lookahead_; }
    float ceiling() const noexcept { return ceiling_; }

    /// Smallest gain applied since the last call (1 = no reduction); resets.
    float takeMinGain() noexcept {
        const float g = minGain_;
        minGain_ = 1.0f;
        return g;
    }

private:
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> delay_;
    int lookahead_{0};  // L
    int window_{1};     // W = L + 1
    float ceiling_{1.0f};
    double releaseCoeff_{0.0};

    // Sliding minimum (monotonic deque in a ring of size window_ + 1).
    std::vector<float> dequeValue_;
    std::vector<long long> dequeIndex_;
    int dequeHead_{0}, dequeSize_{0};
    long long n_{0};

    // Moving average of the released envelope.
    std::vector<double> avgRing_;
    int avgPos_{0};
    double avgSum_{0.0};

    double envelope_{1.0};  // double: a float release stalls ~1e-4 below unity
    float minGain_{1.0f};
};

}  // namespace spacer
