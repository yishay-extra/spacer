#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_events/juce_events.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

#include "core/Project.h"
#include "engine/ClockSelector.h"
#include "engine/Command.h"
#include "engine/EngineProcessor.h"
#include "engine/Feedback.h"
#include "engine/GraphBuilder.h"

namespace spacer {

/// The live engine (SPEC §5): a juce::AudioDeviceManager whose callback runs
/// EngineProcessor, plus the message-thread side — graph (re)building on
/// project and device changes, 30 Hz feedback polling, deletion of retired
/// graphs. All public methods are message-thread only.
///
/// Sample-rate / buffer-size changes rebuild the graph in
/// audioDeviceAboutToStart(), while the callback is not running (state of
/// sources carries over where the rate allows, see Graph::takeStateFrom).
class AudioEngine final : private juce::AudioIODeviceCallback, private juce::Timer {
public:
    struct Status {
        juce::String deviceName;
        double sampleRate{0.0};
        int bufferSize{0};
        int outputChannels{0};
        int deviceLatencySamples{0};  // device input + output latency
        int graphLatencySamples{0};   // limiter look-ahead (SPEC §5.6)
        int xruns{0};
        double cpuLoad{0.0};  // 0..1
        std::uint64_t droppedCommands{0};
        std::uint64_t droppedFeedback{0};
    };

    static constexpr int kFeedbackPollHz = 30;

    AudioEngine();
    ~AudioEngine() override;

    /// Opens the device named in `devices.outputDeviceName` (wildcards as in
    /// juce::AudioDeviceManager; empty = system default) at the requested
    /// rate / buffer size, with all its output channels and up to
    /// `numInputChannels` inputs. Returns an error message, empty on success.
    juce::String openDevice(const DeviceMapping& devices, int numInputChannels = 0);
    void closeDevice();

    /// Structural change: builds a new graph for the running device and
    /// swaps it in (SPEC §5.2). Also used before the device is open.
    void setProject(const Project& project);

    /// Source inputs for graphs built from now on (null = silence). Until
    /// WP4/WP5 provide live inputs this is how tools feed test signals.
    void setInputFactory(GraphBuilder::InputFactory factory) { inputFactory_ = std::move(factory); }

    /// Creates external clocks (SPEC §9; WP12). Graphs built after a change of
    /// project.clock carry the new clock; null / unset = internal clock only.
    void setClockFactory(ClockSelector::Factory factory) { clockSelector_.setFactory(std::move(factory)); }
    /// Clock kind in use by the latest graph (Internal after a fallback).
    ClockKind activeClockKind() const noexcept { return clockSelector_.activeKind(); }

    /// Parameter change, queued for the audio thread. False if dropped.
    bool send(const Command& command) { return processor_.push(command); }

    /// Called on the message thread for every feedback item.
    std::function<void(const Feedback&)> onFeedback;

    Status status() const;
    juce::AudioDeviceManager& deviceManager() noexcept { return deviceManager_; }
    /// The processor behind send(), for control code that talks to it
    /// directly (CueEngine). Same rule: message thread only.
    EngineProcessor& processor() noexcept { return processor_; }

private:
    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData, int numInputChannels,
                                          float* const* outputChannelData, int numOutputChannels,
                                          int numSamples,
                                          const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;
    void timerCallback() override;

    std::unique_ptr<Graph> buildGraph(double sampleRate, int blockSize, int numOutputs);

    juce::AudioDeviceManager deviceManager_;
    EngineProcessor processor_;
    std::optional<Project> project_;
    GraphBuilder::InputFactory inputFactory_;
    ClockSelector clockSelector_;

    // Written in audioDeviceAboutToStart (callback not running), read by the callback.
    juce::AudioIODevice* device_{nullptr};
    std::atomic<double> sampleRate_{0.0};
    std::atomic<int> blockSize_{0};
    std::atomic<int> numOutputs_{0};
    std::atomic<int> xruns_{0};
    std::atomic<bool> running_{false};
};

}  // namespace spacer
