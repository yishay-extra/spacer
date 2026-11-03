#include "engine/HostedPlugin.h"

#include <algorithm>

namespace spacer {

namespace {

juce::AudioChannelSet setFor(int numChannels) {
    return numChannels == 2 ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::mono();
}

/// Ask for `numChannels` in and out on the main buses; plugins that refuse
/// keep their default layout and InsertChain adapts (mono into a stereo
/// plugin is fed to both inputs).
void configureLayout(juce::AudioPluginInstance& p, int numChannels) {
    auto layout = p.getBusesLayout();
    if (!layout.inputBuses.isEmpty()) layout.inputBuses.getReference(0) = setFor(numChannels);
    if (!layout.outputBuses.isEmpty()) layout.outputBuses.getReference(0) = setFor(numChannels);
    if (!p.setBusesLayout(layout) && numChannels == 1) {
        // Mono refused: dual mono in a stereo layout is the next best thing.
        if (!layout.inputBuses.isEmpty()) layout.inputBuses.getReference(0) = setFor(2);
        if (!layout.outputBuses.isEmpty()) layout.outputBuses.getReference(0) = setFor(2);
        p.setBusesLayout(layout);
    }
}

}  // namespace

HostedPlugin::HostedPlugin(std::unique_ptr<juce::AudioPluginInstance> instance, std::string pluginId,
                           int numChannels, double sampleRate, int maxBlockSize,
                           std::shared_ptr<std::atomic<bool>> latencyFlag)
    : instance_(std::move(instance)),
      pluginId_(std::move(pluginId)),
      numChannels_(std::clamp(numChannels, 1, 2)),
      sampleRate_(sampleRate),
      maxBlockSize_(std::max(1, maxBlockSize)),
      latencyFlag_(std::move(latencyFlag)) {
    configureLayout(*instance_, numChannels_);
    instance_->setRateAndBufferSizeDetails(sampleRate_, maxBlockSize_);
    instance_->prepareToPlay(sampleRate_, maxBlockSize_);
    bufferChannels_ = std::max(
        {numChannels_, instance_->getTotalNumInputChannels(), instance_->getTotalNumOutputChannels()});
    latency_ = std::max(0, instance_->getLatencySamples());
    instance_->addListener(this);
}

HostedPlugin::~HostedPlugin() {
    instance_->removeListener(this);
    instance_->releaseResources();
}

void HostedPlugin::audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails& details) {
    // May arrive on any thread: only flag it; PluginHost polls on the message thread.
    if (details.latencyChanged && latencyFlag_) latencyFlag_->store(true, std::memory_order_release);
}

float HostedPlugin::parameter(int index) const noexcept {
    const auto& params = instance_->getParameters();
    if (index < 0 || index >= params.size()) return 0.0f;
    return params.getUnchecked(index)->getValue();
}

bool HostedPlugin::setParameter(int index, float normalisedValue) noexcept {
    const auto& params = instance_->getParameters();
    if (index < 0 || index >= params.size()) return false;
    params.getUnchecked(index)->setValue(std::clamp(normalisedValue, 0.0f, 1.0f));
    return true;
}

}  // namespace spacer
