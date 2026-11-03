#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <memory>
#include <vector>

#include "core/Clock.h"
#include "core/Layout.h"
#include "core/SceneDiff.h"
#include "engine/SourceInput.h"
#include "engine/SourceNode.h"
#include "engine/SpeakerChain.h"

namespace spacer {

/// A complete, pre-allocated processing graph (SPEC §5.2, §5.3): sources,
/// speaker bus, optional ambisonic bus, one speaker chain per layout speaker
/// and the speaker → device-channel map. Built and prepared on the message
/// thread (see GraphBuilder); processed and swapped on the audio thread
/// without allocating.
class Graph {
public:
    /// Non-RT. `ambisonicOrder` 0 = no ambisonic bus.
    Graph(const Layout& layout, double sampleRate, int maxBlockSize, int numOutputChannels,
          int ambisonicOrder);

    // ---- build (non-RT) ----
    /// Prepares the node for this graph's layout and rate.
    void addSource(std::unique_ptr<SourceNode> node);
    /// Speakers must be added in layout order (bus index = layout index).
    void addSpeaker(const SpeakerChainSettings& settings, int outputChannel);

    // ---- queries ----
    double sampleRate() const noexcept { return sampleRate_; }
    int maxBlockSize() const noexcept { return maxBlockSize_; }
    int numOutputChannels() const noexcept { return numOutputs_; }
    int numSources() const noexcept { return static_cast<int>(sources_.size()); }
    int numSpeakers() const noexcept { return static_cast<int>(chains_.size()); }
    int ambisonicOrder() const noexcept { return ambisonicOrder_; }
    const Layout& layout() const noexcept { return layout_; }
    /// Latency added by the graph (limiter look-ahead), in samples.
    int latencySamples() const noexcept;

    SourceNode& source(int index) noexcept { return *sources_[static_cast<std::size_t>(index)]; }
    SpeakerChain& speaker(int index) noexcept { return *chains_[static_cast<std::size_t>(index)]; }
    int outputChannel(int speakerIndex) const noexcept {
        return outputChannels_[static_cast<std::size_t>(speakerIndex)];
    }
    SourceNode* findSource(int id) noexcept;
    SpeakerChain* findSpeaker(int id) noexcept;

    // ---- RT ----
    /// Render one block (numSamples <= maxBlockSize) and ADD each speaker's
    /// output into `outputs[outputChannel]`.
    void process(const InputContext& ctx, float* const* outputs, int numOutputs, int numSamples) noexcept;

    /// Swap with state carry-over (SPEC §5.2). Sources present in both
    /// graphs continue from `old`'s state; speaker chains present in both
    /// (same speaker id and sample rate) are moved over from `old` with all
    /// their signal state and retargeted to this graph's settings. `old` is
    /// left holding whatever it is given in exchange and must only be deleted.
    void takeStateFrom(Graph& old) noexcept;

    /// Scene recall (SPEC §11): like takeStateFrom, but sources in
    /// `diff.replacedSources` crossfade (the old node keeps playing as a
    /// "ghost" that fades out over `crossfadeSamples` while the new node fades
    /// in and continues the same input), sources in `diff.addedSources` fade in
    /// over max(transition, crossfade), and sources in `diff.removedSources`
    /// fade out as ghosts over the same time. Ghosts are freed with the graph
    /// that holds them, never on the audio thread.
    void takeStateForRecall(Graph& old, const SceneDiff& diff, int crossfadeSamples,
                            int transitionSamples) noexcept;

    /// Start fading out (over `numSamples`) every source absent from `next`.
    /// Returns how many sources are fading.
    int fadeOutSourcesMissingFrom(Graph& next, int numSamples) noexcept;

    // ---- clock (SPEC §9: only one clock is active; switching happens
    // through SwapGraph) ----
    /// Non-RT, before the graph is handed to the engine. Selects the clock
    /// this graph runs on: an external ClockSource (owned by the graph, so a
    /// replaced clock is deleted with the retired graph on the message
    /// thread) or, with null, the engine's internal clock. A graph whose
    /// clock was never set keeps whatever clock the graph it replaces uses.
    void setClock(std::unique_ptr<ClockSource> clock) {
        clock_ = std::move(clock);
        clockSet_ = true;
    }
    bool clockSet() const noexcept { return clockSet_; }
    /// The external clock, or null for the internal one.
    ClockSource* clock() noexcept { return clock_.get(); }
    /// If this graph's clock was never set, take over `old`'s selection (and
    /// its ClockSource, with all its state). RT-safe: moves a pointer.
    void inheritClockFrom(Graph& old) noexcept {
        if (clockSet_ || !old.clockSet_) return;
        clock_ = std::move(old.clock_);
        clockSet_ = true;
    }

private:
    void takeSpeakerStateFrom(Graph& old) noexcept;
    void adoptGhost(Graph& old, int sourceId, int fadeSamples) noexcept;

    Layout layout_;
    double sampleRate_;
    int maxBlockSize_;
    int numOutputs_;
    int ambisonicOrder_;

    std::vector<std::unique_ptr<SourceNode>> sources_;
    /// Nodes fading out after a scene recall (still rendered until silent).
    std::vector<std::unique_ptr<SourceNode>> ghosts_;
    std::vector<std::unique_ptr<SpeakerChain>> chains_;
    std::vector<int> outputChannels_;

    juce::AudioBuffer<float> bus_;
    std::vector<float*> busPointers_;
    juce::AudioBuffer<float> ambisonicBus_;
    std::vector<float*> ambisonicPointers_;

    std::unique_ptr<ClockSource> clock_;
    bool clockSet_{false};
};

}  // namespace spacer
