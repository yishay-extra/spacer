#pragma once

namespace spacer {

/// What the audio callback provides to source inputs for one block.
struct InputContext {
    const float* const* deviceInputs{nullptr};
    int numDeviceInputs{0};
};

/// Produces a source's raw signal (SPEC §8): virtual input, hardware input,
/// file, generator. This WP provides only BufferInput (offline rendering);
/// the live kinds arrive with WP4 (hardware, file, generator) and WP5
/// (virtual device). A source without an input renders silence.
class SourceInput {
public:
    virtual ~SourceInput() = default;

    /// Non-RT.
    virtual void prepare(double sampleRate, int maxBlockSize) = 0;

    /// RT. Write `numSamples` into each of `numChannels` (1 or 2) buffers.
    virtual void read(const InputContext& ctx, float* const* dest, int numChannels,
                      int numSamples) noexcept = 0;

    /// RT. Graph swap: continue from `old`'s playback state where that makes
    /// sense (same kind). Default: nothing to carry.
    virtual void takeStateFrom(SourceInput& /*old*/) noexcept {}
};

}  // namespace spacer
