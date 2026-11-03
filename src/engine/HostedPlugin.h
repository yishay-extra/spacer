#pragma once

#include <juce_audio_processors_headless/juce_audio_processors_headless.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

namespace spacer {

/// One loaded plugin instance in an insert slot (SPEC §13), prepared for a
/// fixed sample rate / block size / channel count. Created and destroyed on
/// the message thread (PluginHost); processed on the audio thread through
/// InsertChain.
///
/// Instances are shared (std::shared_ptr) between the graph that is running
/// and a newly built graph, so a structural change elsewhere does not reload
/// the plugin or lose its state (SPEC §5.2). Only one graph processes at a
/// time, but during a crossfade two insert chains may hold the same instance;
/// the audio-thread-only `owner` token decides which of them processes it
/// (the other passes its input through, latency-aligned).
class HostedPlugin : private juce::AudioProcessorListener {
public:
    /// `latencyFlag` (optional) is set when the plugin reports a latency change.
    HostedPlugin(std::unique_ptr<juce::AudioPluginInstance> instance, std::string pluginId, int numChannels,
                 double sampleRate, int maxBlockSize, std::shared_ptr<std::atomic<bool>> latencyFlag = {});
    ~HostedPlugin() override;
    HostedPlugin(const HostedPlugin&) = delete;
    HostedPlugin& operator=(const HostedPlugin&) = delete;

    /// Set when the user edits the plugin (editor gesture end, program or
    /// non-parameter state change) — not by host parameter changes
    /// (commands, ramps, modulators). Message thread, before use.
    void setEditedFlag(std::shared_ptr<std::atomic<bool>> flag) { editedFlag_ = std::move(flag); }

    juce::AudioPluginInstance& instance() noexcept { return *instance_; }
    const std::string& pluginId() const noexcept { return pluginId_; }
    int numChannels() const noexcept { return numChannels_; }
    double sampleRate() const noexcept { return sampleRate_; }
    int maxBlockSize() const noexcept { return maxBlockSize_; }
    /// Channels the plugin's process buffer needs (max of its ins, outs and ours).
    int bufferChannels() const noexcept { return bufferChannels_; }
    /// Latency reported when the instance was prepared (what the graph aligns to).
    int latencySamples() const noexcept { return latency_; }

    // ---- audio thread ----
    std::uint64_t owner() const noexcept { return owner_; }
    void setOwner(std::uint64_t chainId) noexcept { owner_ = chainId; }
    /// Normalised value of parameter `index` (0 if there is none). RT-safe.
    float parameter(int index) const noexcept;
    /// RT-safe for well-behaved plugins (AU parameters are set from the render thread by hosts).
    bool setParameter(int index, float normalisedValue) noexcept;

private:
    void audioProcessorParameterChanged(juce::AudioProcessor*, int, float) override {}
    void audioProcessorParameterChangeGestureEnd(juce::AudioProcessor*, int) override;
    void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails& details) override;

    std::unique_ptr<juce::AudioPluginInstance> instance_;
    std::string pluginId_;
    int numChannels_;
    double sampleRate_;
    int maxBlockSize_;
    int bufferChannels_{1};
    int latency_{0};
    std::shared_ptr<std::atomic<bool>> latencyFlag_;
    std::shared_ptr<std::atomic<bool>> editedFlag_;
    std::uint64_t owner_{0};
};

}  // namespace spacer
