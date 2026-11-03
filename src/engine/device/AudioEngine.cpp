#include "engine/device/AudioEngine.h"

#include <algorithm>

#include "engine/AllocationGuard.h"
#include "engine/GraphBuilder.h"

namespace spacer {

AudioEngine::AudioEngine() {
    streamer_.start();
    deviceManager_.addAudioCallback(this);
    startTimerHz(kFeedbackPollHz);
}

AudioEngine::~AudioEngine() {
    stopTimer();
    deviceManager_.removeAudioCallback(this);
    deviceManager_.closeAudioDevice();
    streamer_.stop();
}

juce::String AudioEngine::openDevice(const DeviceMapping& devices, int numInputChannels) {
    const auto name = juce::String(devices.outputDeviceName);
    auto error = deviceManager_.initialise(std::max(0, numInputChannels), 2, nullptr, true, name);
    if (error.isNotEmpty()) return error;

    auto setup = deviceManager_.getAudioDeviceSetup();
    if (devices.sampleRate > 0.0) setup.sampleRate = devices.sampleRate;
    if (devices.bufferSize > 0) setup.bufferSize = devices.bufferSize;
    if (auto* device = deviceManager_.getCurrentAudioDevice()) {
        setup.useDefaultOutputChannels = false;
        setup.outputChannels.clear();
        setup.outputChannels.setRange(0, device->getOutputChannelNames().size(), true);
        setup.useDefaultInputChannels = false;
        setup.inputChannels.clear();
        setup.inputChannels.setRange(0, std::min(numInputChannels, device->getInputChannelNames().size()),
                                     true);
    }
    return deviceManager_.setAudioDeviceSetup(setup, true);
}

void AudioEngine::closeDevice() {
    deviceManager_.closeAudioDevice();
}

std::unique_ptr<Graph> AudioEngine::buildGraph(double sampleRate, int blockSize, int numOutputs) const {
    if (!project_ || sampleRate <= 0.0 || blockSize <= 0) return nullptr;
    GraphBuilder builder(*project_);
    if (inputFactory_) builder.setInputFactory(inputFactory_);
    builder.setDiskStreamer(&streamer_);
    builder.setProjectDirectory(projectDirectory_);
    builder.setNumOutputChannels(numOutputs);
    warnings_.clear();
    return builder.build(sampleRate, blockSize, &warnings_);
}

void AudioEngine::setProject(const Project& project) {
    project_ = project;
    if (running_) {
        if (auto graph = buildGraph(sampleRate_.load(), blockSize_.load(), numOutputs_.load()))
            processor_.swapGraph(std::move(graph));
        processor_.push(cmd::ClockSetBpm{project.clock.bpm});
    } else {
        processor_.configureClock(project.clock);
    }
}

void AudioEngine::audioDeviceAboutToStart(juce::AudioIODevice* device) {
    // The callback is not running here, so the processor may be touched directly.
    device_ = device;
    const double sr = device->getCurrentSampleRate();
    const int block = device->getCurrentBufferSizeSamples();
    const int outputs = device->getActiveOutputChannels().countNumberOfSetBits();
    sampleRate_ = sr;
    blockSize_ = block;
    numOutputs_ = outputs;
    // Allow for hosts that deliver more than the nominal buffer size.
    if (auto graph = buildGraph(sr, std::max(block, 512), outputs))
        processor_.setGraphWhileStopped(std::move(graph));
    // Also asks the driver to follow this rate (SPEC §14).
    virtual_.prepare(sr, std::max(block, EngineProcessor::kMaxChunk));
    running_ = true;
}

void AudioEngine::audioDeviceStopped() {
    running_ = false;
    device_ = nullptr;
}

void AudioEngine::audioDeviceIOCallbackWithContext(const float* const* in, int numIn, float* const* out,
                                                   int numOut, int numSamples,
                                                   const juce::AudioIODeviceCallbackContext&) {
    if (device_ != nullptr) {
        const int x = device_->getXRunCount();  // atomic read in JUCE's CoreAudio device
        processor_.reportXruns(x);
        xruns_.store(x, std::memory_order_relaxed);
    }
    // Spacer.driver channels for this callback (silence without the driver).
    const bool haveVirtual = numSamples <= virtual_.maxBlockSize();
    if (haveVirtual) {
        allocguard::ScopedAudioThread audioThread;
        virtual_.pull(numSamples);
    }
    processor_.process(in, numIn, out, numOut, numSamples, haveVirtual ? virtual_.channels() : nullptr,
                       haveVirtual ? VirtualDeviceReceiver::kNumChannels : 0);
}

void AudioEngine::timerCallback() {
    Feedback f;
    while (processor_.popFeedback(f))
        if (onFeedback) onFeedback(f);
    processor_.collectGarbage();
    if (virtual_.poll() && onVirtualDeviceChanged) onVirtualDeviceChanged(virtual_.status());
}

AudioEngine::Status AudioEngine::status() const {
    Status s;
    if (auto* d = deviceManager_.getCurrentAudioDevice()) {
        s.deviceName = d->getName();
        s.sampleRate = d->getCurrentSampleRate();
        s.bufferSize = d->getCurrentBufferSizeSamples();
        s.outputChannels = d->getActiveOutputChannels().countNumberOfSetBits();
        s.deviceLatencySamples = d->getInputLatencyInSamples() + d->getOutputLatencyInSamples();
    }
    s.graphLatencySamples = processor_.latencySamples();
    if (const auto v = virtual_.status(); v.state == VirtualDeviceReceiver::State::Connected)
        s.virtualDeviceLatencySamples = v.latencyFrames;
    s.xruns = xruns_.load(std::memory_order_relaxed);
    s.cpuLoad = deviceManager_.getCpuUsage();
    s.droppedCommands = processor_.droppedCommands();
    s.droppedFeedback = processor_.droppedFeedback();
    return s;
}

}  // namespace spacer
