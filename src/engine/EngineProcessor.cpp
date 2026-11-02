#include "engine/EngineProcessor.h"

#include <algorithm>
#include <cstring>
#include <type_traits>
#include <variant>

#include "engine/AllocationGuard.h"

namespace spacer {

namespace {
float masterTarget(float gainDb, bool panic) {
    return panic ? 0.0f : juce::Decibels::decibelsToGain(gainDb, -200.0f);
}
}  // namespace

EngineProcessor::EngineProcessor() {
    master_.prepare(48000.0, 1.0f);
}

EngineProcessor::~EngineProcessor() {
    Command c;
    while (commands_.pop(c))
        if (auto* s = std::get_if<cmd::SwapGraph>(&c)) delete s->graph;
    collectGarbage();
    delete pending_;
    delete graph_;
}

bool EngineProcessor::push(const Command& command) noexcept {
    return commands_.push(command);
}

bool EngineProcessor::swapGraph(std::unique_ptr<Graph> graph) {
    Graph* raw = graph.release();
    if (commands_.push(cmd::SwapGraph{raw})) return true;
    delete raw;
    return false;
}

int EngineProcessor::collectGarbage() {
    int n = 0;
    Graph* g = nullptr;
    while (retired_.pop(g)) {
        delete g;
        ++n;
    }
    return n;
}

void EngineProcessor::setGraphWhileStopped(std::unique_ptr<Graph> graph) {
    // Apply what is queued first, so a SwapGraph sent before the stop (built
    // for the old device settings) cannot replace `graph` later.
    applyCommands();
    collectGarbage();
    delete pending_;
    pending_ = nullptr;
    fadingOut_ = false;
    Graph* old = graph_;
    graph_ = graph.release();
    if (graph_ && old) graph_->takeStateFrom(*old);
    delete old;
    if (graph_) adoptSampleRate(graph_->sampleRate());
    latency_.store(graph_ ? graph_->latencySamples() : 0, std::memory_order_relaxed);
}

void EngineProcessor::adoptSampleRate(double sampleRate) noexcept {
    if (juce::exactlyEqual(sampleRate, sampleRate_)) return;
    sampleRate_ = sampleRate;
    clock_.setSampleRate(sampleRate);
    const float current = master_.current();
    master_.prepare(sampleRate, current);  // GainRamp::prepare does not allocate
    master_.setTarget(masterTarget(masterGainDb_, panic_));
}

void EngineProcessor::applyCommands() noexcept {
    Command c;
    while (commands_.pop(c)) apply(c);
}

void EngineProcessor::apply(const Command& command) noexcept {
    std::visit(
        [this](const auto& c) {
            using C = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<C, cmd::SwapGraph>) {
                // A newer graph supersedes one not yet applied. (The retire
                // queue is emptied at 30 Hz and holds 32, so it is never full
                // in practice; if it were, the newest graph is the one dropped.)
                if (pending_ != nullptr && !retired_.push(pending_)) {
                    retired_.push(c.graph);
                    return;
                }
                pending_ = c.graph;
                fadingOut_ = false;
            } else if constexpr (std::is_same_v<C, cmd::SetMasterGain>) {
                masterGainDb_ = c.gainDb;
                master_.setTarget(masterTarget(masterGainDb_, panic_), c.rampMs);
            } else if constexpr (std::is_same_v<C, cmd::SetPanic>) {
                panic_ = c.engaged;
                master_.setTarget(masterTarget(masterGainDb_, panic_));
            } else if constexpr (std::is_same_v<C, cmd::ClockPlay>) {
                clock_.play();
            } else if constexpr (std::is_same_v<C, cmd::ClockStop>) {
                clock_.stop();
            } else if constexpr (std::is_same_v<C, cmd::ClockContinue>) {
                clock_.resume();
            } else if constexpr (std::is_same_v<C, cmd::ClockSetBpm>) {
                clock_.setBpm(c.bpm);
            } else if constexpr (std::is_same_v<C, cmd::ClockSetBeat>) {
                clock_.setBeat(c.beat);
            } else if constexpr (std::is_same_v<C, cmd::ClockTap>) {
                clock_.tap(clock_.current().timeSeconds);
            } else {
                if (graph_ == nullptr) return;
                if constexpr (std::is_same_v<C, cmd::SetSourceGain>) {
                    if (auto* s = graph_->findSource(c.sourceId)) s->setGainDb(c.gainDb, c.rampMs);
                } else if constexpr (std::is_same_v<C, cmd::SetSourceMute>) {
                    if (auto* s = graph_->findSource(c.sourceId)) s->setMute(c.mute, c.rampMs);
                } else if constexpr (std::is_same_v<C, cmd::SetSourcePosition>) {
                    if (auto* s = graph_->findSource(c.sourceId))
                        s->setPosition(c.channel, c.position, c.smoothMs);
                } else if constexpr (std::is_same_v<C, cmd::SetSourceSpread>) {
                    if (auto* s = graph_->findSource(c.sourceId)) s->setSpread(c.spread);
                } else if constexpr (std::is_same_v<C, cmd::SetSourceWidth>) {
                    if (auto* s = graph_->findSource(c.sourceId)) s->setWidth(c.widthDeg);
                } else if constexpr (std::is_same_v<C, cmd::SetSourceDistance>) {
                    if (auto* s = graph_->findSource(c.sourceId)) s->setDistanceModel(c.model);
                } else if constexpr (std::is_same_v<C, cmd::SetSpeakerTrim>) {
                    if (auto* s = graph_->findSpeaker(c.speakerId)) s->setTrimDb(c.trimDb, c.rampMs);
                } else if constexpr (std::is_same_v<C, cmd::SetSpeakerMute>) {
                    if (auto* s = graph_->findSpeaker(c.speakerId)) s->setMute(c.mute, c.rampMs);
                } else if constexpr (std::is_same_v<C, cmd::SetSpeakerDelay>) {
                    if (auto* s = graph_->findSpeaker(c.speakerId)) s->setUserDelayMs(c.userDelayMs);
                } else if constexpr (std::is_same_v<C, cmd::SetSpeakerPolarity>) {
                    if (auto* s = graph_->findSpeaker(c.speakerId)) s->setPolarityInverted(c.inverted);
                } else if constexpr (std::is_same_v<C, cmd::SetSpeakerEq>) {
                    if (auto* s = graph_->findSpeaker(c.speakerId)) s->setEq(c.eq);
                } else {
                    static_assert(sizeof(C) == 0, "unhandled command");
                }
            }
        },
        command);
}

bool EngineProcessor::applyPendingSwap() noexcept {
    if (pending_ == nullptr) return false;
    if (graph_ != nullptr) {
        // The old graph must be able to go back for deletion; otherwise wait.
        if (retired_.size() >= SpscQueue<Graph*, kRetireCapacity>::capacity()) return false;
        pending_->takeStateFrom(*graph_);
        retired_.push(graph_);
    }
    graph_ = pending_;
    pending_ = nullptr;
    fadingOut_ = false;
    adoptSampleRate(graph_->sampleRate());
    latency_.store(graph_->latencySamples(), std::memory_order_relaxed);
    return true;
}

void EngineProcessor::process(const float* const* inputs, int numInputs, float* const* outputs,
                              int numOutputs, int numSamples) noexcept {
    allocguard::ScopedAudioThread audioThread;

    numInputs = std::clamp(numInputs, 0, kMaxIoChannels);
    numOutputs = std::clamp(numOutputs, 0, kMaxIoChannels);
    for (int c = 0; c < numOutputs; ++c)
        if (outputs[c]) std::fill(outputs[c], outputs[c] + numSamples, 0.0f);
    if (numSamples <= 0) return;

    applyCommands();

    // Structural change (SPEC §5.2): sources missing from the new graph are
    // faded out over this block in the old graph; the swap happens at the
    // start of the next block. Without such sources it happens right away.
    bool swapAfterBlock = false;
    if (pending_ != nullptr) {
        if (fadingOut_ || graph_ == nullptr) {
            applyPendingSwap();
        } else if (graph_->fadeOutSourcesMissingFrom(*pending_, numSamples) > 0) {
            fadingOut_ = true;
            swapAfterBlock = true;
        } else {
            applyPendingSwap();
        }
    }

    clock_.advance(numSamples);
    // Motions (WP7) evaluate here with a MotionContext built from the clock.

    if (graph_ != nullptr) {
        const int chunkMax = std::min(graph_->maxBlockSize(), kMaxChunk);
        for (int offset = 0; offset < numSamples; offset += chunkMax) {
            const int n = std::min(chunkMax, numSamples - offset);
            for (int c = 0; c < numInputs; ++c)
                inputChunk_[static_cast<std::size_t>(c)] = inputs[c] ? inputs[c] + offset : nullptr;
            for (int c = 0; c < numOutputs; ++c)
                outputChunk_[static_cast<std::size_t>(c)] = outputs[c] ? outputs[c] + offset : nullptr;
            const InputContext ctx{inputChunk_.data(), numInputs};
            graph_->process(ctx, outputChunk_.data(), numOutputs, n);

            // Master gain / panic ramp, identical on every output (SPEC §5.3).
            if (master_.isRamping()) {
                for (int i = 0; i < n; ++i) masterRamp_[static_cast<std::size_t>(i)] = master_.next();
                for (int c = 0; c < numOutputs; ++c)
                    if (float* o = outputChunk_[static_cast<std::size_t>(c)])
                        for (int i = 0; i < n; ++i) o[i] *= masterRamp_[static_cast<std::size_t>(i)];
            } else if (const float g = master_.current(); !juce::exactlyEqual(g, 1.0f)) {
                for (int c = 0; c < numOutputs; ++c)
                    if (float* o = outputChunk_[static_cast<std::size_t>(c)])
                        for (int i = 0; i < n; ++i) o[i] *= g;
            }
        }
    }

    samplesToFeedback_ -= numSamples;
    if (samplesToFeedback_ <= 0) {
        publishFeedback();
        samplesToFeedback_ =
            static_cast<long long>((sampleRate_ > 0.0 ? sampleRate_ : 48000.0) / kFeedbackRateHz);
    }

    if (swapAfterBlock) applyPendingSwap();
}

void EngineProcessor::publishFeedback() noexcept {
    feedback_.push(fb::Clock{clock_.current()});
    if (xruns_ != xrunsReported_) {
        xrunsReported_ = xruns_;
        feedback_.push(fb::Xrun{xruns_});
    }
    if (const auto dropped = commands_.dropped(); dropped != droppedReported_) {
        droppedReported_ = dropped;
        feedback_.push(fb::CommandDropped{dropped});
    }
    if (graph_ == nullptr) return;
    for (int i = 0; i < graph_->numSources(); ++i) {
        SourceNode& s = graph_->source(i);
        fb::SourceMeter meter{s.id(), {s.takePeak(0), s.takePeak(1)}};
        feedback_.push(meter);
        for (int c = 0; c < s.numChannels(); ++c)
            feedback_.push(fb::SourcePosition{s.id(), c, s.renderedPosition(c)});
    }
    for (int i = 0; i < graph_->numSpeakers(); ++i) {
        SpeakerChain& sp = graph_->speaker(i);
        const float minGain = sp.takeMinLimiterGain();
        feedback_.push(fb::SpeakerMeter{sp.speakerId(), sp.takePeak(),
                                        juce::Decibels::gainToDecibels(minGain, -100.0f)});
    }
}

}  // namespace spacer
