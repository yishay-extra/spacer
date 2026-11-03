#include "engine/GraphBuilder.h"

#include <algorithm>
#include <utility>
#include <variant>

#include "engine/PluginHost.h"
#include "engine/SourceInputFactory.h"
#include "engine/SpatializerFactory.h"

namespace spacer {

GraphBuilder::GraphBuilder(Project project) : project_(std::move(project)) {}

GraphBuilder& GraphBuilder::setInputFactory(InputFactory factory) {
    inputFactory_ = std::move(factory);
    return *this;
}

GraphBuilder& GraphBuilder::setPluginHost(PluginHost* host) {
    pluginHost_ = host;
    return *this;
}

namespace {

std::unique_ptr<InsertChain> buildInserts(PluginHost* host, InsertOwner owner, int id,
                                          const std::vector<PluginSlot>& slots, int numChannels,
                                          double sampleRate, int maxBlockSize) {
    if (slots.empty()) return nullptr;
    if (host != nullptr) return host->buildChain(owner, id, slots, numChannels, sampleRate, maxBlockSize);
    auto chain = std::make_unique<InsertChain>();  // no host: every slot passes through
    for (std::size_t i = 0; i < slots.size() && i < static_cast<std::size_t>(kMaxInserts); ++i)
        chain->addSlot(nullptr, slots[i].pluginId, slots[i].bypassed);
    return chain;
}

}  // namespace

GraphBuilder& GraphBuilder::setDiskStreamer(DiskStreamer* streamer) {
    streamer_ = streamer;
    return *this;
}

GraphBuilder& GraphBuilder::setProjectDirectory(const juce::File& directory) {
    projectDirectory_ = directory;
    return *this;
}

GraphBuilder& GraphBuilder::setNumOutputChannels(int numChannels) {
    numOutputChannels_ = numChannels;
    return *this;
}

int GraphBuilder::requiredOutputChannels(const Layout& layout) {
    int n = 0;
    for (const auto& s : layout.speakers) n = std::max(n, s.outputChannel + 1);
    return n;
}

int GraphBuilder::requiredInputChannels(const Project& project) {
    int n = 0;
    for (const auto& s : project.sources)
        if (const auto* hw = std::get_if<HardwareInputSpec>(&s.input.value))
            n = std::max({n, hw->left + 1, hw->right + 1});
    return n;
}

int GraphBuilder::ambisonicBusOrder(const Project& project) {
    for (const auto& s : project.sources)
        if (s.spatializer == SpatializerKind::Ambisonic) return std::clamp(project.ambisonics.order, 1, 5);
    return 0;
}

std::vector<SpeakerChainSettings> GraphBuilder::chainSettings(const Project& project) {
    const auto compensation = computeCompensation(project.layout, project.devices.speedOfSound);
    std::vector<SpeakerChainSettings> out;
    out.reserve(project.layout.speakers.size());
    for (std::size_t i = 0; i < project.layout.speakers.size(); ++i) {
        const Speaker& sp = project.layout.speakers[i];
        SpeakerChainSettings s;
        s.speakerId = sp.id;
        s.compensationDelayMs = compensation[i].delayMs;
        s.compensationGainDb = compensation[i].gainDb;
        s.userDelayMs = sp.userDelayMs;
        s.trimDb = sp.trimDb;
        s.polarityInverted = sp.polarityInverted;
        for (const auto& c : project.speakerChains) {
            if (c.speakerId != sp.id) continue;
            s.userDelayMs = c.userDelayMs;
            s.trimDb = c.trimDb;
            s.polarityInverted = c.polarityInverted;
            s.mute = c.mute;
            s.eq = c.eq;
            break;
        }
        out.push_back(s);
    }
    return out;
}

std::unique_ptr<Graph> GraphBuilder::build(double sampleRate, int maxBlockSize,
                                           std::vector<std::string>* warnings) const {
    const Layout& layout = project_.layout;
    const int outputs = numOutputChannels_ >= 0 ? numOutputChannels_ : requiredOutputChannels(layout);
    auto graph =
        std::make_unique<Graph>(layout, sampleRate, maxBlockSize, outputs, ambisonicBusOrder(project_));

    const auto chains = chainSettings(project_);
    const auto numSpeakers = std::min<std::size_t>(chains.size(), kMaxSpeakers);
    for (std::size_t i = 0; i < numSpeakers; ++i) {
        const int speakerId = layout.speakers[i].id;
        const std::vector<PluginSlot>* slots = nullptr;
        for (const auto& c : project_.speakerChains)
            if (c.speakerId == speakerId) slots = &c.inserts;
        graph->addSpeaker(chains[i], layout.speakers[i].outputChannel,
                          slots ? buildInserts(pluginHost_, InsertOwner::Speaker, speakerId, *slots, 1,
                                               sampleRate, maxBlockSize)
                                : nullptr);
    }

    SourceInputFactory liveInputs;
    liveInputs.setDiskStreamer(streamer_);
    liveInputs.setProjectDirectory(projectDirectory_);

    const auto numSources = std::min<std::size_t>(project_.sources.size(), kMaxSources);
    for (std::size_t i = 0; i < numSources; ++i) {
        const SourceSpec& spec = project_.sources[i];
        auto input = inputFactory_ ? inputFactory_(spec) : nullptr;
        if (!input) input = liveInputs.create(spec, warnings);
        auto node = std::make_unique<SourceNode>(spec, makeSpatializer(spec), std::move(input));
        node->setInserts(std::make_unique<InsertStage>(buildInserts(pluginHost_, InsertOwner::Source, spec.id,
                                                                    spec.inserts, spec.stereo ? 2 : 1,
                                                                    sampleRate, maxBlockSize)));
        graph->addSource(std::move(node));
    }
    graph->alignInsertLatencies();
    return graph;
}

}  // namespace spacer
