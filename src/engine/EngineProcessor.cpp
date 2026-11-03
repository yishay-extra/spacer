#include "engine/EngineProcessor.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <type_traits>
#include <variant>

#include "engine/AllocationGuard.h"

namespace spacer {

namespace {
float masterTarget(float gainDb, bool panic) {
    return panic ? 0.0f : juce::Decibels::decibelsToGain(gainDb, -200.0f);
}

/// The EQ field a ramp target addresses, or null if it is not an EQ target.
float* eqField(Eq3& e, RampTarget t) {
    if (t == RampTarget::SpeakerEqLowFreq) return &e.lowFreq;
    if (t == RampTarget::SpeakerEqLowGainDb) return &e.lowGainDb;
    if (t == RampTarget::SpeakerEqMidFreq) return &e.midFreq;
    if (t == RampTarget::SpeakerEqMidGainDb) return &e.midGainDb;
    if (t == RampTarget::SpeakerEqMidQ) return &e.midQ;
    if (t == RampTarget::SpeakerEqHighFreq) return &e.highFreq;
    if (t == RampTarget::SpeakerEqHighGainDb) return &e.highGainDb;
    return nullptr;
}

}  // namespace

EngineProcessor::EngineProcessor() {
    master_.prepare(48000.0, 1.0f);
}

EngineProcessor::~EngineProcessor() {
    QueuedCommand q;
    while (commands_.pop(q)) {
        if (auto* s = std::get_if<cmd::SwapGraph>(&q.command)) delete s->graph;
        if (auto* r = std::get_if<cmd::RecallScene>(&q.command)) delete r->plan;
    }
    for (int i = 0; i < numTimed_; ++i) {
        auto& c = timed_[static_cast<std::size_t>(i)].command;
        if (auto* s = std::get_if<cmd::SwapGraph>(&c)) delete s->graph;
        if (auto* r = std::get_if<cmd::RecallScene>(&c)) delete r->plan;
    }
    collectGarbage();
    delete scheduled_;
    delete pending_;
    delete graph_;
}

bool EngineProcessor::push(const Command& command) noexcept {
    return commands_.push(QueuedCommand{command, -1});
}

bool EngineProcessor::pushAt(const Command& command, std::int64_t engineSample) noexcept {
    return commands_.push(QueuedCommand{command, std::max<std::int64_t>(engineSample, -1)});
}

std::int64_t EngineProcessor::sampleAtHostTime(double hostSeconds) const noexcept {
    const Snapshot s = snapshot_.load();
    if (s.hostTimeSeconds < 0.0 || s.sampleRate <= 0.0) return -1;
    return s.blockStartSample +
           static_cast<std::int64_t>(std::llround((hostSeconds - s.hostTimeSeconds) * s.sampleRate));
}

bool EngineProcessor::swapGraph(std::unique_ptr<Graph> graph) {
    Graph* raw = graph.release();
    if (commands_.push(QueuedCommand{cmd::SwapGraph{raw}, -1})) return true;
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
    RecallPlan* p = nullptr;
    while (retiredPlans_.pop(p)) {
        delete p;
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
    if (graph_ && old) {
        graph_->takeStateFrom(*old);
        graph_->inheritClockFrom(*old);
    }
    selectClock();
    delete old;
    if (graph_) adoptSampleRate(graph_->sampleRate());
    latency_.store(graph_ ? graph_->latencySamples() : 0, std::memory_order_relaxed);
}

void EngineProcessor::adoptSampleRate(double sampleRate) noexcept {
    if (juce::exactlyEqual(sampleRate, sampleRate_)) return;
    sampleRate_ = sampleRate;
    clock_.setSampleRate(sampleRate);
    if (external_ != nullptr) external_->setSampleRate(sampleRate);
    const float current = master_.current();
    master_.prepare(sampleRate, current);  // GainRamp::prepare does not allocate
    master_.setTarget(masterTarget(masterGainDb_, panic_));
}

// ---- clock (SPEC §9) -----------------------------------------------------------

void EngineProcessor::selectClock() noexcept {
    // external_ may point into a graph that is already retired: it is only
    // compared here, never dereferenced.
    ClockSource* next = graph_ != nullptr ? graph_->clock() : nullptr;
    if (next == external_) return;
    if (next != nullptr) {
        next->setSampleRate(sampleRate_ > 0.0 ? sampleRate_ : graph_->sampleRate());
    } else {
        // Back to the internal clock: continue from where the external one
        // was, so motions and beat-triggered cues do not jump.
        clock_.setBpm(clockState_.bpm);
        clock_.setBeat(clockState_.beat);
        if (clockState_.playing)
            clock_.resume();
        else
            clock_.stop();
    }
    external_ = next;
}

ClockState EngineProcessor::advanceClock(int numSamples) noexcept {
    // The internal clock always runs: it owns the engine timeline
    // (timeSeconds, used by tap tempo and the cue engine).
    ClockState s = clock_.advance(numSamples);
    if (external_ != nullptr) {
        const double engineTime = s.timeSeconds;
        s = external_->advance(numSamples);
        s.timeSeconds = engineTime;
    }
    clockState_ = s;
    return s;
}

void EngineProcessor::applyCommands() noexcept {
    QueuedCommand q;
    while (commands_.pop(q)) {
        if (q.atSample > samplePosition_)
            insertTimed(q);
        else
            apply(q.command);
    }
}

void EngineProcessor::insertTimed(const QueuedCommand& q) noexcept {
    if (numTimed_ >= kMaxTimedCommands) {
        apply(q.command);  // no room to wait: late rather than lost
        return;
    }
    // Sorted by sample; equal samples keep their push order.
    int i = numTimed_;
    while (i > 0 && timed_[static_cast<std::size_t>(i - 1)].atSample > q.atSample) {
        timed_[static_cast<std::size_t>(i)] = timed_[static_cast<std::size_t>(i - 1)];
        --i;
    }
    timed_[static_cast<std::size_t>(i)] = q;
    ++numTimed_;
}

void EngineProcessor::applyTimedUpTo(std::int64_t sample) noexcept {
    int done = 0;
    while (done < numTimed_ && timed_[static_cast<std::size_t>(done)].atSample <= sample)
        apply(timed_[static_cast<std::size_t>(done++)].command);
    if (done == 0) return;
    for (int i = done; i < numTimed_; ++i)
        timed_[static_cast<std::size_t>(i - done)] = timed_[static_cast<std::size_t>(i)];
    numTimed_ -= done;
}

int EngineProcessor::nextTimedOffset(std::int64_t blockStart, int numSamples) const noexcept {
    if (numTimed_ == 0) return -1;
    const std::int64_t offset = timed_[0].atSample - blockStart;
    return offset < numSamples ? static_cast<int>(std::max<std::int64_t>(offset, 0)) : -1;
}

void EngineProcessor::apply(const Command& command) noexcept {
    std::visit(
        [this](const auto& c) {
            using C = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<C, cmd::SwapGraph>) {
                // A newer graph supersedes one not yet applied. (The retire
                // queue is emptied at 30 Hz and holds 32, so it is never full
                // in practice; if it were, the newest graph is the one dropped.)
                // A clock switch carried by the superseded graph is kept.
                if (pending_ != nullptr && c.graph != nullptr) c.graph->inheritClockFrom(*pending_);
                if (pending_ != nullptr && !retired_.push(pending_)) {
                    retired_.push(c.graph);
                    return;
                }
                pending_ = c.graph;
                fadingOut_ = false;
            } else if constexpr (std::is_same_v<C, cmd::RecallScene>) {
                if (c.plan == nullptr) return;
                if (c.plan->atBeat >= 0.0)
                    scheduleRecall(c.plan);
                else
                    applyRecall(c.plan);
            } else if constexpr (std::is_same_v<C, cmd::CancelScheduledRecall>) {
                if (scheduled_ != nullptr) retirePlan(scheduled_);
                scheduled_ = nullptr;
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
                } else if constexpr (std::is_same_v<C, cmd::SetMotionParam>) {
                    if (auto* s = graph_->findSource(c.sourceId))
                        s->setMotionParam(c.channel, c.param, c.value);
                } else if constexpr (std::is_same_v<C, cmd::ResetMotion>) {
                    if (auto* s = graph_->findSource(c.sourceId)) s->resetMotion();
                } else if constexpr (std::is_same_v<C, cmd::SetModulatorValue>) {
                    if (auto* s = graph_->findSource(c.sourceId)) s->setModulatorValue(c.index, c.value);
                } else if constexpr (std::is_same_v<C, cmd::FilePlay>) {
                    if (auto* s = graph_->findSource(c.sourceId))
                        if (auto* in = s->input()) in->play();
                } else if constexpr (std::is_same_v<C, cmd::FileStop>) {
                    if (auto* s = graph_->findSource(c.sourceId))
                        if (auto* in = s->input()) in->stop();
                } else if constexpr (std::is_same_v<C, cmd::FileSeek>) {
                    if (auto* s = graph_->findSource(c.sourceId))
                        if (auto* in = s->input()) in->seek(c.seconds);
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
                } else if constexpr (std::is_same_v<C, cmd::SetInsertParam>) {
                    if (auto* st = graph_->findInserts(c.owner, c.id))
                        st->chain().setParameter(c.slot, c.index, c.value);
                } else if constexpr (std::is_same_v<C, cmd::SetInsertBypass>) {
                    if (auto* st = graph_->findInserts(c.owner, c.id))
                        st->chain().setBypassed(c.slot, c.bypassed);
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
        // An ambisonic decoder crossfade in progress finishes first (one at a time).
        if (graph_->isAmbisonicCrossfading()) return false;
        pending_->takeStateFrom(*graph_);
        pending_->inheritClockFrom(*graph_);  // before the old graph can be deleted
        retired_.push(graph_);
    }
    graph_ = pending_;
    pending_ = nullptr;
    fadingOut_ = false;
    selectClock();
    adoptSampleRate(graph_->sampleRate());
    latency_.store(graph_->latencySamples(), std::memory_order_relaxed);
    return true;
}

void EngineProcessor::process(const float* const* inputs, int numInputs, float* const* outputs,
                              int numOutputs, int numSamples, const float* const* virtualInputs,
                              int numVirtualInputs) noexcept {
    allocguard::ScopedAudioThread audioThread;

    numInputs = std::clamp(numInputs, 0, kMaxIoChannels);
    numVirtualInputs = virtualInputs != nullptr ? std::clamp(numVirtualInputs, 0, kMaxIoChannels) : 0;
    numOutputs = std::clamp(numOutputs, 0, kMaxIoChannels);
    for (int c = 0; c < numOutputs; ++c)
        if (outputs[c]) std::fill(outputs[c], outputs[c] + numSamples, 0.0f);
    if (numSamples <= 0) return;

    const std::int64_t blockStart = samplePosition_;
    const double hostTime = nextHostTime_;
    nextHostTime_ = -1.0;
    applyCommands();
    applyTimedUpTo(blockStart);

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

    const ClockState clockAtStart = advanceClock(numSamples);
    // Clock first (SPEC §9); each chunk's motions see the clock at its start.

    // A scheduled (AtBeat) recall fires at the exact sample where the transport
    // reaches its beat: the block is split there (SPEC §9 startOffset).
    int recallAt = scheduledRecallOffset(clockAtStart, numSamples);

    for (int offset = 0; offset < numSamples;) {
        if (offset == recallAt) {
            RecallPlan* plan = scheduled_;
            scheduled_ = nullptr;
            recallAt = -1;
            applyRecall(plan);
        }
        // Timed commands (OSC timetags) split the block at their sample.
        applyTimedUpTo(blockStart + offset);
        const int timedAt = nextTimedOffset(blockStart, numSamples);
        int limit = recallAt > offset ? recallAt : numSamples;
        if (timedAt > offset) limit = std::min(limit, timedAt);
        if (graph_ == nullptr) {
            offset = limit;
            continue;
        }
        const int chunkMax = std::min(graph_->maxBlockSize(), kMaxChunk);
        const int n = std::min(chunkMax, limit - offset);
        for (int c = 0; c < numInputs; ++c)
            inputChunk_[static_cast<std::size_t>(c)] = inputs[c] ? inputs[c] + offset : nullptr;
        for (int c = 0; c < numOutputs; ++c)
            outputChunk_[static_cast<std::size_t>(c)] = outputs[c] ? outputs[c] + offset : nullptr;
        for (int c = 0; c < numVirtualInputs; ++c)
            virtualChunk_[static_cast<std::size_t>(c)] =
                virtualInputs[c] ? virtualInputs[c] + offset : nullptr;
        const InputContext ctx{inputChunk_.data(), numInputs, virtualChunk_.data(), numVirtualInputs};
        stepRamps(n);
        ClockState chunkClock = clockAtStart;
        if (offset > 0 && sampleRate_ > 0.0) {
            const double dt = offset / sampleRate_;
            chunkClock.timeSeconds += dt;
            if (chunkClock.playing) chunkClock.beat += dt * chunkClock.bpm / 60.0;
        }
        graph_->process(ctx, chunkClock, outputChunk_.data(), numOutputs, n);

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
        offset += n;
    }

    samplePosition_ = blockStart + numSamples;
    snapshot_.store(Snapshot{clockAtStart, blockStart, hostTime, sampleRate_});

    samplesToFeedback_ -= numSamples;
    if (samplesToFeedback_ <= 0) {
        publishFeedback();
        samplesToFeedback_ =
            static_cast<long long>((sampleRate_ > 0.0 ? sampleRate_ : 48000.0) / kFeedbackRateHz);
    }

    if (swapAfterBlock) applyPendingSwap();
}

// ---- scene recall (SPEC §11) -------------------------------------------------

void EngineProcessor::retirePlan(RecallPlan* plan) noexcept {
    // Sized so that it cannot fill between two collectGarbage() calls; if it
    // ever did, the plan is leaked rather than freed on the audio thread.
    retiredPlans_.push(plan);
}

void EngineProcessor::scheduleRecall(RecallPlan* plan) noexcept {
    if (scheduled_ != nullptr) retirePlan(scheduled_);
    scheduled_ = plan;
}

int EngineProcessor::scheduledRecallOffset(const ClockState& clock, int numSamples) const noexcept {
    if (scheduled_ == nullptr) return -1;
    double beat = clock.beat;
    int segmentStart = 0;
    if (clock.startOffset >= 0) {
        beat = clock.startBeat;
        segmentStart = clock.startOffset;
    } else if (!clock.playing) {
        return -1;
    }
    constexpr double kEps = 1e-9;
    const double target = scheduled_->atBeat;
    if (target < beat - kEps) return -1;  // already behind the transport
    const double beatsPerSample = clock.bpm / 60.0 / clock.sampleRate;
    const double sample =
        static_cast<double>(segmentStart) + std::ceil((target - beat) / beatsPerSample - 1e-6);
    if (sample >= static_cast<double>(numSamples)) return -1;
    return std::max(segmentStart, static_cast<int>(sample));
}

void EngineProcessor::applyRecall(RecallPlan* plan) noexcept {
    // A SwapGraph not yet applied is applied first, so the recall acts on the
    // newest graph.
    if (pending_ != nullptr) applyPendingSwap();

    const double sr = sampleRate_ > 0.0 ? sampleRate_ : 48000.0;
    const int transitionSamples =
        static_cast<int>(std::lround(static_cast<double>(plan->transitionSeconds) * sr));
    const int crossfadeSamples =
        static_cast<int>(std::lround(static_cast<double>(kRecallCrossfadeMs) * 0.001 * sr));

    if (plan->graph) {
        const bool canRetire =
            graph_ == nullptr || retired_.size() < SpscQueue<Graph*, kRetireCapacity>::capacity();
        if (!canRetire) {
            retirePlan(plan);  // cannot swap now; the recall is dropped (the queue is drained at 30 Hz)
            return;
        }
        if (graph_ != nullptr) {
            plan->graph->takeStateForRecall(*graph_, plan->diff, crossfadeSamples, transitionSamples);
            plan->graph->inheritClockFrom(*graph_);
            retired_.push(graph_);
        }
        graph_ = plan->graph.release();
        selectClock();
        adoptSampleRate(graph_->sampleRate());
        latency_.store(graph_->latencySamples(), std::memory_order_relaxed);
    }

    // Gains ramp for exactly the transition (never shorter than the default
    // click-free 10 ms).
    const float rampMs = std::max(plan->transitionSeconds * 1000.0f, GainRamp::kDefaultRampMs);
    for (const Ramp& r : plan->diff.ramps) startRamp(r, transitionSamples, rampMs);

    if (plan->restartMotionPhase && graph_ != nullptr)
        for (int i = 0; i < graph_->numSources(); ++i) graph_->source(i).restartMotion();

    if (plan->token != 0) feedback_.push(fb::CueFired{plan->token});
    retirePlan(plan);
}

void EngineProcessor::startRamp(const Ramp& r, int totalSamples, float rampMs) noexcept {
    const RampTarget t = r.target;
    if (t == RampTarget::MasterGainDb) {
        masterGainDb_ = r.to;
        master_.setTarget(masterTarget(masterGainDb_, panic_), rampMs);
        return;
    }
    if (graph_ == nullptr) return;

    // Sample-accurate ramps and switches the nodes already know how to do.
    if (t == RampTarget::SourceGainDb || t == RampTarget::SourceMute) {
        if (auto* s = graph_->findSource(r.id)) {
            if (t == RampTarget::SourceGainDb)
                s->setGainDb(r.to, rampMs);
            else
                s->setMute(r.to > 0.5f, rampMs);
        }
        return;
    }
    if (t == RampTarget::SpeakerTrimDb || t == RampTarget::SpeakerMute || t == RampTarget::SpeakerPolarity ||
        t == RampTarget::SpeakerUserDelayMs) {
        if (auto* s = graph_->findSpeaker(r.id)) {
            if (t == RampTarget::SpeakerTrimDb)
                s->setTrimDb(r.to, rampMs);
            else if (t == RampTarget::SpeakerMute)
                s->setMute(r.to > 0.5f, rampMs);
            else if (t == RampTarget::SpeakerPolarity)
                s->setPolarityInverted(r.to > 0.5f);
            else
                s->setUserDelayMs(r.to);
        }
        return;
    }

    // Insert slots: bypass is a discrete, crossfaded switch; parameters ramp
    // from the plugin's live value.
    const bool insertParam = t == RampTarget::SourceInsertParam || t == RampTarget::SpeakerInsertParam;
    const bool insertBypass = t == RampTarget::SourceInsertBypass || t == RampTarget::SpeakerInsertBypass;
    InsertStage* inserts = nullptr;
    if (insertParam || insertBypass) {
        const bool speaker = t == RampTarget::SpeakerInsertParam || t == RampTarget::SpeakerInsertBypass;
        inserts = graph_->findInserts(speaker ? InsertOwner::Speaker : InsertOwner::Source, r.id);
        if (inserts == nullptr) return;
        if (insertBypass) {
            inserts->chain().setBypassed(r.slot, r.to > 0.5f);
            return;
        }
    }

    // Everything else is stepped once per chunk, linearly from the live value.
    ActiveRamp a;
    a.ramp = r;
    a.total = totalSamples;
    a.from = r.from;
    a.fromPosition = r.fromPosition;
    if (inserts != nullptr) {
        if (inserts->chain().plugin(r.slot) == nullptr) return;  // missing plugin
        a.from = inserts->chain().parameter(r.slot, r.param);
    } else if (auto* s = graph_->findSource(r.id)) {
        if (t == RampTarget::SourceSpread)
            a.from = s->spread();
        else if (t == RampTarget::SourceWidth)
            a.from = s->width();
        else if (t == RampTarget::SourceDistanceK)
            a.from = s->distanceModel().k;
        else if (t == RampTarget::SourceDistanceRMin)
            a.from = s->distanceModel().rMin;
        else if (t == RampTarget::SourceDistanceQ)
            a.from = s->distanceModel().q;
        else if (t == RampTarget::SourcePosition)
            a.fromPosition = s->controlPosition(std::max(0, r.channel));
    } else if (auto* sp = graph_->findSpeaker(r.id)) {
        Eq3 e = sp->settings().eq;
        if (const float* field = eqField(e, t)) a.from = *field;
    } else {
        return;  // the source / speaker is not in this graph
    }

    if (totalSamples <= 0) {
        applyRampValue(r, r.to, r.toPosition);
        return;
    }
    // A newer ramp for the same parameter replaces the older one.
    for (int i = 0; i < numRamps_; ++i) {
        ActiveRamp& old = ramps_[static_cast<std::size_t>(i)];
        if (old.ramp.target == t && old.ramp.id == r.id && old.ramp.channel == r.channel &&
            old.ramp.slot == r.slot && old.ramp.param == r.param) {
            old = a;
            return;
        }
    }
    if (numRamps_ >= kMaxActiveRamps) {
        applyRampValue(r, r.to, r.toPosition);
        return;
    }
    ramps_[static_cast<std::size_t>(numRamps_++)] = a;
}

void EngineProcessor::applyRampValue(const Ramp& r, float v, const Position& position) noexcept {
    if (graph_ == nullptr) return;
    const RampTarget t = r.target;
    if (t == RampTarget::SourceInsertParam || t == RampTarget::SpeakerInsertParam) {
        const auto owner = t == RampTarget::SpeakerInsertParam ? InsertOwner::Speaker : InsertOwner::Source;
        if (auto* st = graph_->findInserts(owner, r.id)) st->chain().setParameter(r.slot, r.param, v);
        return;
    }
    if (auto* s = graph_->findSource(r.id)) {
        if (t == RampTarget::SourceSpread) {
            s->setSpread(v);
        } else if (t == RampTarget::SourceWidth) {
            s->setWidth(v);
        } else if (t == RampTarget::SourceDistanceK || t == RampTarget::SourceDistanceRMin ||
                   t == RampTarget::SourceDistanceQ) {
            DistanceModel m = s->distanceModel();
            if (t == RampTarget::SourceDistanceK)
                m.k = v;
            else if (t == RampTarget::SourceDistanceRMin)
                m.rMin = v;
            else
                m.q = v;
            s->setDistanceModel(m);
        } else if (t == RampTarget::SourcePosition) {
            s->setPosition(r.channel, position, 0.0f);
        }
    } else if (auto* sp = graph_->findSpeaker(r.id)) {
        Eq3 e = sp->settings().eq;
        if (float* field = eqField(e, t)) {
            *field = v;
            sp->setEq(e);
        }
    }
}

void EngineProcessor::stepRamps(int numSamples) noexcept {
    for (int i = 0; i < numRamps_;) {
        ActiveRamp& a = ramps_[static_cast<std::size_t>(i)];
        a.elapsed += numSamples;
        const bool done = a.elapsed >= a.total;
        const float t = done ? 1.0f : static_cast<float>(a.elapsed) / static_cast<float>(a.total);
        Position p = a.ramp.toPosition;
        if (!done && a.ramp.target == RampTarget::SourcePosition) {
            const Cartesian f = a.fromPosition.toCartesian();
            const Cartesian g = a.ramp.toPosition.toCartesian();
            p = Position::fromCartesian(f + (g - f) * t);
        }
        applyRampValue(a.ramp, done ? a.ramp.to : a.from + (a.ramp.to - a.from) * t, p);
        if (done)
            ramps_[static_cast<std::size_t>(i)] = ramps_[static_cast<std::size_t>(--numRamps_)];
        else
            ++i;
    }
}

void EngineProcessor::publishFeedback() noexcept {
    feedback_.push(fb::Clock{clockState_});
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
        TransportState t;
        if (const SourceInput* in = s.input(); in && in->transportState(t))
            feedback_.push(fb::FileTransport{s.id(), t.positionSeconds, t.playing, t.underruns});
    }
    for (int i = 0; i < graph_->numSpeakers(); ++i) {
        SpeakerChain& sp = graph_->speaker(i);
        const float minGain = sp.takeMinLimiterGain();
        feedback_.push(fb::SpeakerMeter{sp.speakerId(), sp.takePeak(),
                                        juce::Decibels::gainToDecibels(minGain, -100.0f)});
    }
}

}  // namespace spacer
