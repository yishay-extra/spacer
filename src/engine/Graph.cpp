#include "engine/Graph.h"

#include <algorithm>
#include <utility>

#include "core/SpecEquality.h"

namespace spacer {

Graph::Graph(const Layout& layout, double sampleRate, int maxBlockSize, int numOutputChannels,
             int ambisonicOrder)
    : layout_(layout),
      sampleRate_(sampleRate),
      maxBlockSize_(std::max(1, maxBlockSize)),
      numOutputs_(std::max(0, numOutputChannels)),
      ambisonicOrder_(std::clamp(ambisonicOrder, 0, 5)) {
    const int numSpeakers = std::min(static_cast<int>(layout.speakers.size()), kMaxSpeakers);
    bus_.setSize(std::max(1, numSpeakers), maxBlockSize_);
    bus_.clear();
    busPointers_.resize(static_cast<std::size_t>(bus_.getNumChannels()));
    for (int c = 0; c < bus_.getNumChannels(); ++c)
        busPointers_[static_cast<std::size_t>(c)] = bus_.getWritePointer(c);

    if (ambisonicOrder_ > 0) {
        const int n = (ambisonicOrder_ + 1) * (ambisonicOrder_ + 1);
        ambisonicBus_.setSize(n, maxBlockSize_);
        ambisonicBus_.clear();
        ambisonicPointers_.resize(static_cast<std::size_t>(n));
        for (int c = 0; c < n; ++c)
            ambisonicPointers_[static_cast<std::size_t>(c)] = ambisonicBus_.getWritePointer(c);
    }
    sources_.reserve(kMaxSources);
    ghosts_.reserve(2 * kMaxSources);
    chains_.reserve(kMaxSpeakers);
    outputChannels_.reserve(kMaxSpeakers);
}

void Graph::addSource(std::unique_ptr<SourceNode> node) {
    node->prepare(layout_, sampleRate_, maxBlockSize_);
    sources_.push_back(std::move(node));
}

void Graph::addSpeaker(const SpeakerChainSettings& settings, int outputChannel,
                       std::unique_ptr<InsertChain> inserts) {
    auto chain = std::make_unique<SpeakerChain>();
    chain->setInserts(std::make_unique<InsertStage>(std::move(inserts)));
    chain->prepare(sampleRate_, maxBlockSize_);
    chain->configure(settings);
    chains_.push_back(std::move(chain));
    outputChannels_.push_back(outputChannel);
}

int Graph::latencySamples() const noexcept {
    const int limiter = chains_.empty() ? 0 : chains_.front()->limiterLatencySamples();
    return limiter + sourceInsertLatency_ + speakerInsertLatency_;
}

void Graph::alignInsertLatencies() {
    bool sourceInserts = false, speakerInserts = false;
    sourceInsertLatency_ = speakerInsertLatency_ = 0;
    for (auto& s : sources_) {
        sourceInserts = sourceInserts || !s->inserts().chain().empty();
        sourceInsertLatency_ = std::max(sourceInsertLatency_, s->inserts().pluginLatency());
    }
    for (auto& c : chains_) {
        speakerInserts = speakerInserts || !c->inserts().chain().empty();
        speakerInsertLatency_ = std::max(speakerInsertLatency_, c->inserts().pluginLatency());
    }
    // Chains of a graph without any inserts of that kind need no padding at
    // all; otherwise every chain gets room to absorb later latency changes.
    const auto capacity = [](bool any, int maxLatency) {
        return any ? std::max(InsertChain::kMinPadCapacity, maxLatency) : 0;
    };
    for (auto& s : sources_)
        s->inserts().configurePad(sourceInsertLatency_ - s->inserts().pluginLatency(),
                                  capacity(sourceInserts, sourceInsertLatency_));
    for (auto& c : chains_)
        c->inserts().configurePad(speakerInsertLatency_ - c->inserts().pluginLatency(),
                                  capacity(speakerInserts, speakerInsertLatency_));
}

InsertStage* Graph::findInserts(InsertOwner owner, int id) noexcept {
    if (owner == InsertOwner::Source) {
        SourceNode* s = findSource(id);
        return s != nullptr ? &s->inserts() : nullptr;
    }
    SpeakerChain* c = findSpeaker(id);
    return c != nullptr ? &c->inserts() : nullptr;
}

void Graph::claimInserts() noexcept {
    // The new graph's chains process the shared plugin instances from now on;
    // ghosts and outgoing chains holding the same instances pass through.
    for (auto& s : sources_) s->inserts().claim();
    for (auto& c : chains_) c->inserts().claim();
}

SourceNode* Graph::findSource(int id) noexcept {
    for (auto& s : sources_)
        if (s && s->id() == id) return s.get();
    return nullptr;
}

SpeakerChain* Graph::findSpeaker(int id) noexcept {
    for (auto& c : chains_)
        if (c->speakerId() == id) return c.get();
    return nullptr;
}

void Graph::process(const InputContext& ctx, const ClockState& clock, float* const* outputs, int numOutputs,
                    int n) noexcept {
    const int numSpeakers = static_cast<int>(chains_.size());
    for (int s = 0; s < bus_.getNumChannels(); ++s)
        std::fill(busPointers_[static_cast<std::size_t>(s)], busPointers_[static_cast<std::size_t>(s)] + n,
                  0.0f);
    for (auto* p : ambisonicPointers_) std::fill(p, p + n, 0.0f);

    SpeakerBus bus;
    bus.speakers = busPointers_.data();
    bus.numSpeakers = numSpeakers;
    bus.ambisonic = ambisonicPointers_.empty() ? nullptr : ambisonicPointers_.data();
    bus.ambisonicOrder = ambisonicOrder_;
    bus.numSamples = n;

    for (auto& s : sources_) s->process(ctx, clock, n, bus);
    for (auto& g : ghosts_)
        if (!g->isSilent()) g->process(ctx, clock, n, bus);

    // Ambisonic decode into the speaker bus: WP2d (AmbisonicDecoder).

    for (int s = 0; s < numSpeakers; ++s) {
        float* sig = busPointers_[static_cast<std::size_t>(s)];
        chains_[static_cast<std::size_t>(s)]->process(sig, n);
        const int out = outputChannels_[static_cast<std::size_t>(s)];
        if (out < 0 || out >= numOutputs || outputs[out] == nullptr) continue;
        float* dst = outputs[out];
        for (int i = 0; i < n; ++i) dst[i] += sig[i];
    }
}

void Graph::takeStateFrom(Graph& old) noexcept {
    const bool sameLayout = specEqual(layout_, old.layout_);
    for (auto& s : sources_)
        if (SourceNode* o = old.findSource(s->id())) s->takeStateFrom(*o, sameLayout);

    takeSpeakerStateFrom(old);
    claimInserts();
}

void Graph::takeSpeakerStateFrom(Graph& old) noexcept {
    if (!juce::exactlyEqual(old.sampleRate_, sampleRate_)) return;
    for (auto& fresh : chains_) {
        for (auto& previous : old.chains_) {
            if (previous->speakerId() != fresh->speakerId()) continue;
            // Keep the running chain (delay contents, filters, limiter) and
            // send it towards the new settings; the unused fresh chain goes
            // back with the old graph.
            previous->retargetTo(*fresh);
            std::swap(previous, fresh);
            break;
        }
    }
}

void Graph::adoptGhost(Graph& old, int sourceId, int fadeSamples) noexcept {
    if (ghosts_.size() >= ghosts_.capacity()) return;  // would allocate; the node just goes away with `old`
    for (auto& s : old.sources_) {
        if (!s || s->id() != sourceId) continue;
        s->fadeOut(fadeSamples);
        ghosts_.push_back(std::move(s));
        return;
    }
}

void Graph::takeStateForRecall(Graph& old, const SceneDiff& diff, int crossfadeSamples,
                               int transitionSamples) noexcept {
    const auto listed = [](const std::vector<int>& ids, int id) {
        return std::find(ids.begin(), ids.end(), id) != ids.end();
    };
    const int slowFade = std::max(crossfadeSamples, transitionSamples);
    const bool sameLayout = specEqual(layout_, old.layout_);

    for (auto& s : sources_) {
        const int id = s->id();
        SourceNode* previous = old.findSource(id);
        if (previous == nullptr) {
            s->fadeIn(slowFade);
        } else if (listed(diff.replacedSources, id)) {
            s->takeInputStateFrom(*previous);
            s->fadeIn(crossfadeSamples);
            adoptGhost(old, id, crossfadeSamples);
        } else {
            s->takeStateFrom(*previous, sameLayout);
        }
    }
    for (int id : diff.removedSources) adoptGhost(old, id, slowFade);

    // Ghosts still fading from an earlier recall keep going.
    for (auto& g : old.ghosts_)
        if (g && !g->isSilent() && ghosts_.size() < ghosts_.capacity()) ghosts_.push_back(std::move(g));

    takeSpeakerStateFrom(old);
    claimInserts();
}

int Graph::fadeOutSourcesMissingFrom(Graph& next, int numSamples) noexcept {
    int count = 0;
    for (auto& s : sources_) {
        if (next.findSource(s->id()) != nullptr || juce::exactlyEqual(s->currentGain(), 0.0f)) continue;
        s->fadeOut(numSamples);
        ++count;
    }
    return count;
}

}  // namespace spacer
