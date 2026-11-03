#include "TestPlugins.h"

#include <cstdlib>

namespace spacer::testing {

std::atomic<int> TestPlugin::liveInstances{0};
std::atomic<int> TestPlugin::createdInstances{0};
std::vector<juce::String> TestPluginFormat::candidates{TestPluginFormat::id("gain"),
                                                       TestPluginFormat::id("delay64")};

namespace {

/// "delay64" -> ("delay", 64); "gain" -> ("gain", 0).
std::pair<juce::String, int> parseKind(const juce::String& identifier) {
    const juce::String kind = identifier.fromFirstOccurrenceOf(":", false, false);
    if (kind.startsWith("delay")) return {"delay", kind.substring(5).getIntValue()};
    return {kind, 0};
}

bool knownKind(const juce::String& kind) {
    return kind == "gain" || kind == "delay" || kind == "swap" || kind == "fail";
}

void describe(juce::PluginDescription& d, const juce::String& identifier) {
    const auto [kind, delay] = parseKind(identifier);
    d.name = "Spacer test " + kind + (delay > 0 ? juce::String(delay) : juce::String());
    d.descriptiveName = d.name;
    d.pluginFormatName = TestPluginFormat::kName;
    d.category = "Effect";
    d.manufacturerName = "Spacer";
    d.version = "1.0";
    d.fileOrIdentifier = identifier;
    d.uniqueId = d.deprecatedUid = static_cast<int>(identifier.hashCode());
    d.isInstrument = false;
    d.numInputChannels = 2;
    d.numOutputChannels = 2;
}

}  // namespace

// ---- plugin --------------------------------------------------------------------

TestPlugin::TestPlugin(juce::String kind, int delaySamples, bool stereoOnly)
    : juce::AudioPluginInstance(
          BusesProperties()
              .withInput("In", stereoOnly ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::mono())
              .withOutput("Out",
                          stereoOnly ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::mono())),
      kind_(std::move(kind)),
      delay_(std::max(0, delaySamples)),
      stereoOnly_(stereoOnly) {
    auto gain = std::make_unique<Param>("gain", 0.5f);
    gain_ = gain.get();
    addHostedParameter(std::move(gain));
    setLatencySamples(delay_);
    ++liveInstances;
    ++createdInstances;
}

TestPlugin::~TestPlugin() {
    --liveInstances;
}

void TestPlugin::fillInPluginDescription(juce::PluginDescription& d) const {
    describe(d, TestPluginFormat::id(kind_ + (kind_ == "delay" ? juce::String(delay_) : juce::String())));
}

bool TestPlugin::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    if (in != out) return false;
    if (stereoOnly_) return in == juce::AudioChannelSet::stereo();
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void TestPlugin::prepareToPlay(double, int) {
    delay_ = std::max(0, getLatencySamples());
    line_.setSize(std::max(1, getTotalNumOutputChannels()), delay_ + 1);
    line_.clear();
    writePos_ = 0;
}

void TestPlugin::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    const int channels = std::min(buffer.getNumChannels(), line_.getNumChannels());
    const int n = buffer.getNumSamples();
    const float g = gain();
    if (kind_ == "swap" && buffer.getNumChannels() >= 2) {
        float* l = buffer.getWritePointer(0);
        float* r = buffer.getWritePointer(1);
        for (int i = 0; i < n; ++i) std::swap(l[i], r[i]);
    }
    if (delay_ > 0) {
        const int size = line_.getNumSamples();
        for (int i = 0; i < n; ++i) {
            const int readPos = (writePos_ + 1) % size;  // oldest sample: exactly delay_ ago
            for (int c = 0; c < channels; ++c) {
                float* x = buffer.getWritePointer(c);
                line_.setSample(c, writePos_, x[i]);
                x[i] = line_.getSample(c, readPos);
            }
            writePos_ = (writePos_ + 1) % size;
        }
    }
    buffer.applyGain(g);
    ++processed_;
}

void TestPlugin::getStateInformation(juce::MemoryBlock& dest) {
    juce::MemoryOutputStream out(dest, false);
    out.writeString("SPTS");
    out.writeFloat(gain_->getValue());
    out.writeString(memo);
}

void TestPlugin::setStateInformation(const void* data, int size) {
    juce::MemoryInputStream in(data, static_cast<size_t>(size), false);
    if (in.readString() != "SPTS") return;
    gain_->setValue(in.readFloat());
    memo = in.readString();
}

// ---- format --------------------------------------------------------------------

void TestPluginFormat::findAllTypesForFile(juce::OwnedArray<juce::PluginDescription>& results,
                                           const juce::String& identifier) {
    const auto kind = parseKind(identifier).first;
    if (kind == "crash") std::abort();
    if (kind == "hang")
        for (;;) juce::Thread::sleep(1000);
    if (!knownKind(kind)) return;
    auto* d = new juce::PluginDescription();
    describe(*d, identifier);
    results.add(d);
}

bool TestPluginFormat::fileMightContainThisPluginType(const juce::String& identifier) {
    return identifier.startsWith(juce::String(kName) + ":");
}

juce::String TestPluginFormat::getNameOfPluginFromIdentifier(const juce::String& identifier) {
    return identifier.fromFirstOccurrenceOf(":", false, false);
}

juce::StringArray TestPluginFormat::searchPathsForPlugins(const juce::FileSearchPath&, bool, bool) {
    juce::StringArray out;
    for (const auto& c : candidates) out.add(c);
    return out;
}

void TestPluginFormat::createPluginInstance(const juce::PluginDescription& d, double, int,
                                            PluginCreationCallback callback) {
    const auto [kind, delay] = parseKind(d.fileOrIdentifier);
    if (!knownKind(kind) || kind == "fail") {
        callback(nullptr, "test plugin cannot be instantiated");
        return;
    }
    callback(std::make_unique<TestPlugin>(kind, delay, kind == "swap"), {});
}

void addTestFormat(juce::AudioPluginFormatManager& formats) {
    formats.addFormat(std::make_unique<TestPluginFormat>());
}

}  // namespace spacer::testing
