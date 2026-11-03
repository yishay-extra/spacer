#pragma once

#include <juce_dsp/juce_dsp.h>

namespace spacer {

/// Whole-sample delay used to pad insert chains to a common latency
/// (SPEC §5.6). A change of delay crossfades between the old and the new
/// read position over kCrossfadeMs (as speaker delays do, SPEC §5.4), so it
/// never clicks. prepare() allocates; the rest is real-time safe.
class CompensationDelay {
public:
    static constexpr float kCrossfadeMs = 50.0f;

    /// `capacity` = largest delay, in samples, this instance will ever be
    /// asked for. Capacity 0 makes the delay a no-op (nothing is allocated).
    void prepare(double sampleRate, int maxBlockSize, int numChannels, int capacity);

    int capacity() const noexcept { return capacity_; }
    int delay() const noexcept { return target_; }

    /// Jump to `samples` without a crossfade and clear the history. Non-RT
    /// use only (before the first block).
    void reset(int samples);
    /// RT. Move to `samples` (clamped to capacity) with a crossfade.
    void setDelay(int samples) noexcept;

    /// RT. In place on `numChannels` (<= the prepared count) channels.
    void process(float* const* channels, int numChannels, int numSamples) noexcept;

private:
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> line_;
    int numChannels_{0};
    int capacity_{0};
    int current_{0};
    int target_{0};
    int from_{0};
    int xfadeLength_{1};
    int xfadePos_{-1};
};

}  // namespace spacer
