#pragma once

// In-process fake plugins for the plugin-hosting tests (WP10). They are real
// juce::AudioPluginInstance subclasses behind a real juce::AudioPluginFormat
// ("SpacerTest"), so PluginHost, the scanner and the insert chains run the
// same code paths as with AudioUnits, on any platform.
//
// Identifiers ("SpacerTest:<kind>"):
//   gain       mono/stereo, parameter 0 "gain" (normalised v -> linear 2v; default 0.5 = unity)
//   delay<N>   like gain, plus a pure N-sample delay; reports latency N (e.g. "delay64")
//   swap       stereo-only: swaps L and R (mono sources are fed dual mono)
//   fail       scans fine, but instantiation fails
//   crash      findAllTypesForFile aborts the process (scanner crash)
//   hang       findAllTypesForFile never returns (scanner timeout)

#include <juce_audio_processors_headless/juce_audio_processors_headless.h>

#include <atomic>
#include <string>
#include <vector>

namespace spacer::testing {

class TestPlugin : public juce::AudioPluginInstance {
public:
    TestPlugin(juce::String kind, int delaySamples, bool stereoOnly);
    ~TestPlugin() override;

    /// Instances alive right now (to tell reuse from reload).
    static std::atomic<int> liveInstances;
    /// Instances ever created.
    static std::atomic<int> createdInstances;

    /// A plain normalised parameter.
    class Param : public HostedParameter {
    public:
        Param(juce::String id, float defaultValue)
            : id_(std::move(id)), value_(defaultValue), default_(defaultValue) {}
        float getValue() const override { return value_.load(); }
        void setValue(float v) override { value_.store(v); }
        float getDefaultValue() const override { return default_; }
        juce::String getName(int) const override { return id_; }
        juce::String getLabel() const override { return {}; }
        float getValueForText(const juce::String& text) const override { return text.getFloatValue(); }
        juce::String getParameterID() const override { return id_; }

    private:
        juce::String id_;
        std::atomic<float> value_;
        float default_;
    };

    float gain() const noexcept { return 2.0f * gain_->getValue(); }
    Param& gainParameter() noexcept { return *gain_; }
    /// Free-form text kept in the state blob.
    juce::String memo;
    /// Changes the reported latency (listeners see latencyChanged); the
    /// actual delay follows at the next prepareToPlay.
    void reportLatency(int samples) { setLatencySamples(samples); }
    int processedBlocks() const noexcept { return processed_.load(); }

    void fillInPluginDescription(juce::PluginDescription& d) const override;
    const juce::String getName() const override { return "Spacer test " + kind_; }
    void prepareToPlay(double sampleRate, int maxBlockSize) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    using juce::AudioProcessor::processBlock;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& dest) override;
    void setStateInformation(const void* data, int size) override;

private:
    juce::String kind_;
    int delay_;
    bool stereoOnly_;
    Param* gain_;
    juce::AudioBuffer<float> line_;
    int writePos_{0};
    std::atomic<int> processed_{0};
};

class TestPluginFormat : public juce::AudioPluginFormat {
public:
    static constexpr const char* kName = "SpacerTest";
    /// What searchPathsForPlugins() returns (the "installed" plugins).
    static std::vector<juce::String> candidates;
    /// Identifier for a kind, e.g. id("delay64") = "SpacerTest:delay64".
    static juce::String id(const juce::String& kind) { return juce::String(kName) + ":" + kind; }

    juce::String getName() const override { return kName; }
    void findAllTypesForFile(juce::OwnedArray<juce::PluginDescription>& results,
                             const juce::String& fileOrIdentifier) override;
    bool fileMightContainThisPluginType(const juce::String& fileOrIdentifier) override;
    juce::String getNameOfPluginFromIdentifier(const juce::String& fileOrIdentifier) override;
    bool pluginNeedsRescanning(const juce::PluginDescription&) override { return false; }
    bool doesPluginStillExist(const juce::PluginDescription&) override { return true; }
    bool canScanForPlugins() const override { return true; }
    bool isTrivialToScan() const override { return false; }
    juce::StringArray searchPathsForPlugins(const juce::FileSearchPath&, bool, bool) override;
    juce::FileSearchPath getDefaultLocationsToSearch() override { return {}; }
    bool requiresUnblockedMessageThreadDuringCreation(const juce::PluginDescription&) const override {
        return false;
    }

private:
    void createPluginInstance(const juce::PluginDescription&, double initialSampleRate, int initialBufferSize,
                              PluginCreationCallback callback) override;
};

/// Format setup for PluginHost: just the test format.
void addTestFormat(juce::AudioPluginFormatManager& formats);

}  // namespace spacer::testing
