#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "engine/SourceInput.h"

namespace spacer {

/// Plays a pre-loaded buffer once, then silence. Used by OfflineRenderer
/// (SPEC §5.7) and tests. A mono buffer feeding a stereo source goes to both
/// channels; a multi-channel buffer feeding a mono source is averaged.
class BufferInput final : public SourceInput {
public:
    explicit BufferInput(juce::AudioBuffer<float> audio);

    void prepare(double sampleRate, int maxBlockSize) override;
    void read(const InputContext& ctx, float* const* dest, int numChannels, int numSamples) noexcept override;
    void takeStateFrom(SourceInput& old) noexcept override;

    int position() const noexcept { return position_; }

private:
    juce::AudioBuffer<float> audio_;
    int position_{0};
};

}  // namespace spacer
