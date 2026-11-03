#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <memory>

#include "engine/InsertChain.h"

namespace spacer {

/// The insert point of a source (pre-gain, pre-spatializer) or a speaker
/// (post-EQ, pre-limiter), SPEC §5.3. Holds the running InsertChain and,
/// while a structural insert change crossfades (SPEC §11: insert add/remove
/// switch with a 50 ms crossfade), the outgoing one.
///
/// Graph swaps follow the engine's carry-over pattern: the stage of the
/// running graph is kept and retargeted to the freshly built one; whatever
/// it gives up goes back with the old graph and is freed on the message
/// thread. Nothing here allocates or frees after prepare().
class InsertStage {
public:
    static constexpr float kCrossfadeMs = 50.0f;

    /// Non-RT. `chain` may be null (no inserts; padding only).
    explicit InsertStage(std::unique_ptr<InsertChain> chain = nullptr);

    /// Non-RT. Prepares the chain (if it is not yet) and the crossfade scratch.
    void prepare(double sampleRate, int maxBlockSize, int numChannels);
    /// Non-RT. Padding of the chain to the graph's common latency, with the
    /// capacity of the padding delay (see InsertChain::kMinPadCapacity).
    void configurePad(int padSamples, int padCapacity);

    /// The running chain. A stage that handed its chain over in retargetTo()
    /// (it then belongs to a retired graph) has none and must only be destroyed.
    InsertChain& chain() noexcept { return *current_; }
    const InsertChain& chain() const noexcept { return *current_; }
    bool hasChain() const noexcept { return current_ != nullptr; }
    /// Latency of the plugins (excluding padding).
    int pluginLatency() const noexcept { return current_ ? current_->latencySamples() : 0; }
    bool crossfading() const noexcept { return xfadePos_ >= 0; }

    // ---- RT ----
    /// Continue as `fresh` (the same slot of a newly built graph): keep the
    /// running chain if it has the same instances (adopting bypass flags and
    /// padding), otherwise make `fresh`'s chain current and crossfade from
    /// the running one. Chains given up end up in `fresh`.
    void retargetTo(InsertStage& fresh) noexcept;
    /// Claim the current chain's instances (see HostedPlugin owner).
    void claim() noexcept {
        if (current_) current_->claim();
    }
    void process(float* const* channels, int numChannels, int numSamples) noexcept;

private:
    std::unique_ptr<InsertChain> current_;
    std::unique_ptr<InsertChain> outgoing_;
    juce::AudioBuffer<float> scratch_;
    double sampleRate_{48000.0};
    int numChannels_{1};
    int maxBlockSize_{0};
    int xfadeLength_{1};
    int xfadePos_{-1};
};

}  // namespace spacer
