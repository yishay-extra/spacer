#pragma once

#include <cstdint>

namespace spacer {

/// What the audio callback provides to source inputs for one block.
struct InputContext {
    const float* const* deviceInputs{nullptr};
    int numDeviceInputs{0};
    /// Spacer.driver channels for this block (VirtualDeviceReceiver), null if
    /// none (driver absent, offline render): VirtualInput sources read silence.
    const float* const* virtualInputs{nullptr};
    int numVirtualInputs{0};
};

/// Transport state of an input that has one (file players).
struct TransportState {
    double positionSeconds{0.0};  // file time of the next sample to be played
    bool playing{false};
    std::uint32_t underruns{0};
};

/// Produces a source's raw signal (SPEC §8): virtual input (WP5), hardware
/// input, file, generator; BufferInput for offline rendering and tests. A
/// source without an input renders silence.
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
    /// True once another input's takeStateFrom() moved this input's playback
    /// (a file's disk stream) away: it can no longer produce the same audio.
    /// A scene-recall ghost holding such an input mirrors the new node
    /// instead (SourceNode::setInputMirror).
    virtual bool handedOver() const noexcept { return false; }

    // ---- transport (RT; only file players have one) ----
    virtual void play() noexcept {}
    virtual void stop() noexcept {}
    virtual void seek(double /*seconds*/) noexcept {}
    /// False if the input has no transport.
    virtual bool transportState(TransportState& /*out*/) const noexcept { return false; }
};

}  // namespace spacer
