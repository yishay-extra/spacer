#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>

#include "core/Source.h"
#include "engine/CompensationDelay.h"
#include "engine/GainRamp.h"
#include "engine/HostedPlugin.h"

namespace spacer {

/// One configuration of a source's or speaker's insert slots (SPEC §13):
/// up to kMaxInserts plugins in series, then a padding delay that brings the
/// chain to the latency the graph aligns every chain to (SPEC §5.6).
///
/// - A slot whose plugin is missing (not found / failed to load) passes its
///   input through with no latency ("slot bypassed", SPEC §13).
/// - A bypassed slot crossfades (kBypassFadeMs) between the plugin's output
///   and its input delayed by the plugin's latency, so bypassing never changes
///   the chain's latency or clicks. The plugin keeps running while bypassed
///   (its state stays warm for un-bypass).
/// - A slot whose plugin is held by another chain (owner token, see
///   HostedPlugin) passes through latency-aligned instead of processing it.
///
/// Built on the message thread (PluginHost::buildChain); prepare() allocates;
/// everything else is real-time safe.
class InsertChain {
public:
    static constexpr float kBypassFadeMs = 20.0f;
    /// Padding capacity every chain of a graph with inserts gets at least,
    /// so later latency changes can usually be absorbed in place.
    static constexpr int kMinPadCapacity = 8192;

    InsertChain();
    InsertChain(const InsertChain&) = delete;
    InsertChain& operator=(const InsertChain&) = delete;

    // ---- build (non-RT) ----
    /// `plugin` null = missing plugin. `pluginId` is kept for diagnostics.
    void addSlot(std::shared_ptr<HostedPlugin> plugin, std::string pluginId, bool bypassed);
    /// Allocates scratch / delay lines. `padCapacity` as for CompensationDelay.
    void prepare(double sampleRate, int maxBlockSize, int numChannels, int padCapacity);
    /// Non-RT: set the padding without a crossfade (fresh chains).
    void resetPad(int samples) { pad_.reset(samples); }

    // ---- queries ----
    std::uint64_t id() const noexcept { return id_; }
    int numSlots() const noexcept { return numSlots_; }
    int numChannels() const noexcept { return numChannels_; }
    bool empty() const noexcept { return numSlots_ == 0; }
    /// Sum of the loaded plugins' latencies (missing slots add none).
    int latencySamples() const noexcept { return latency_; }
    int padSamples() const noexcept { return pad_.delay(); }
    int padCapacity() const noexcept { return pad_.capacity(); }
    HostedPlugin* plugin(int slot) const noexcept;
    const std::string& pluginId(int slot) const noexcept;
    bool isMissing(int slot) const noexcept {
        return slot >= 0 && slot < numSlots_ && plugin(slot) == nullptr;
    }
    bool isBypassed(int slot) const noexcept;
    /// Same plugin instances in the same slots (so one can continue as the other).
    bool sameInstancesAs(const InsertChain& other) const noexcept;

    // ---- RT ----
    /// Make this chain the processor of all its instances.
    void claim() noexcept;
    void setPadSamples(int samples) noexcept { pad_.setDelay(samples); }
    void setBypassed(int slot, bool bypassed) noexcept;
    bool setParameter(int slot, int index, float value) noexcept;
    float parameter(int slot, int index) const noexcept;
    /// Take bypass flags (with the usual fade) from a chain with the same instances.
    void adoptSettingsFrom(const InsertChain& other) noexcept;
    /// In place on `numChannels` (== the prepared count) channels.
    void process(float* const* channels, int numChannels, int numSamples) noexcept;

private:
    using Delay = juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None>;

    struct Slot {
        std::shared_ptr<HostedPlugin> plugin;
        std::string pluginId;
        bool bypassed{false};
        int latency{0};
        GainRamp bypassMix;  // 0 = plugin output, 1 = (delayed) input
        Delay dry;
    };

    void processSlot(Slot& s, float* const* ch, int numChannels, int n) noexcept;

    std::uint64_t id_;
    std::array<Slot, kMaxInserts> slots_;
    int numSlots_{0};
    int numChannels_{1};
    int maxBlockSize_{0};
    int latency_{0};
    CompensationDelay pad_;
    juce::AudioBuffer<float> io_;   // plugin process buffer (plugin's channel count)
    juce::AudioBuffer<float> dry_;  // latency-aligned input of the current slot
    juce::MidiBuffer midi_;
};

}  // namespace spacer
