#include "engine/SourceNode.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <variant>

#include "core/SpecEquality.h"
#include "core/motion/MotionFactory.h"
#include "core/motion/MotionParamValue.h"
#include "core/motion/ParameterPath.h"

namespace spacer {

namespace {

float smoothMsOf(const MotionSpec& spec) {
    if (const auto* m = std::get_if<ManualMotion>(&spec.value)) return m->smoothMs;
    return SourceNode::kDefaultPositionSmoothMs;
}

Position manualPositionOf(const MotionSpec& spec) {
    if (const auto* m = std::get_if<ManualMotion>(&spec.value)) return m->position;
    return Position{};
}

bool usesLayout(const MotionSpec& spec) {
    return std::holds_alternative<StepSequenceMotion>(spec.value);
}

}  // namespace

SourceNode::SourceNode(const SourceSpec& spec, std::unique_ptr<Spatializer> spatializer,
                       std::unique_ptr<SourceInput> input)
    : id_(spec.id),
      stereo_(spec.stereo),
      linkedPoints_(spec.linkedPoints),
      kind_(spec.spatializer),
      gainDb_(spec.gainDb),
      mute_(spec.mute),
      spread_(std::clamp(spec.spread, 0.0f, 1.0f)),
      width_(spec.width),
      distanceModel_(spec.distance),
      spatializer_(std::move(spatializer)),
      input_(std::move(input)),
      modulatorSpecs_(spec.modulators),
      insertSpecs_(spec.inserts) {
    motionSpec_[0] = spec.motion;
    motionSpec_[1] = (linkedPoints_ && stereo_ && spec.motionRight) ? *spec.motionRight : spec.motion;
    for (std::size_t k = 0; k < 2; ++k) {
        target_[k] = manualPositionOf(motionSpec_[k]).normalised();
        defaultSmoothMs_[k] = smoothMs_[k] = smoothMsOf(motionSpec_[k]);
        smoothed_[k] = target_[k].toCartesian();
    }
    hasMotionRight_ = linkedPoints_ && stereo_ && spec.motionRight.has_value();
    modulators_.configure(modulatorSpecs_);
    modulatesGain_ = modulators_.targets(SourceParam::GainDb);
}

void SourceNode::setModulator(int index, const ModulatorBank::Live& m, int count) noexcept {
    modulators_.setLive(index, m, count);
    modulatesGain_ = modulators_.targets(SourceParam::GainDb);
}

bool SourceNode::setModulatedInsertBase(int slot, int index, float value) noexcept {
    for (auto& m : insertMods_)
        if (m.active && m.slot == slot && m.index == index) {
            m.base = std::clamp(value, 0.0f, 1.0f);
            m.haveBase = true;
            return true;
        }
    return false;
}

void SourceNode::applyInsertModulation(const ModulatorBank::Output& mod) noexcept {
    InsertChain& chain = inserts_->chain();
    // Parameters no longer modulated go back to their base.
    for (auto& m : insertMods_) {
        if (!m.active) continue;
        bool still = false;
        for (int i = 0; i < mod.numInserts; ++i)
            still = still || (mod.inserts[static_cast<std::size_t>(i)].slot == m.slot &&
                              mod.inserts[static_cast<std::size_t>(i)].index == m.index);
        if (!still) {
            if (m.haveBase) chain.setParameter(m.slot, m.index, m.base);
            m = InsertMod{};
        }
    }
    for (int i = 0; i < mod.numInserts; ++i) {
        const auto& o = mod.inserts[static_cast<std::size_t>(i)];
        if (chain.plugin(o.slot) == nullptr) continue;  // empty / missing slot
        InsertMod* state = nullptr;
        for (auto& m : insertMods_)
            if (m.active && m.slot == o.slot && m.index == o.index) state = &m;
        if (state == nullptr) {
            for (auto& m : insertMods_)
                if (!m.active && state == nullptr) state = &m;
            if (state == nullptr) continue;
            *state = InsertMod{true, o.slot, o.index, 0.0f, false};
        }
        if (!state->haveBase) {
            state->base = chain.parameter(o.slot, o.index);
            if (o.slot >= 0 && o.slot < static_cast<int>(insertSpecs_.size())) {
                const auto& params = insertSpecs_[static_cast<std::size_t>(o.slot)].params;
                if (const auto it = params.find(o.index); it != params.end()) state->base = it->second;
            }
            state->haveBase = true;
        }
        chain.setParameter(o.slot, o.index, std::clamp(state->base + o.value, 0.0f, 1.0f));
    }
}

void SourceNode::prepare(const Layout& layout, double sampleRate, int maxBlockSize) {
    sampleRate_ = sampleRate;
    if (spatializer_) spatializer_->prepare(layout, sampleRate, maxBlockSize);
    if (input_) input_->prepare(sampleRate, maxBlockSize);
    if (!inserts_) inserts_ = std::make_unique<InsertStage>();
    inserts_->prepare(sampleRate, maxBlockSize, numChannels());
    buffer_.setSize(2, std::max(1, maxBlockSize));
    buffer_.clear();
    raw_.setSize(2, std::max(1, maxBlockSize));
    raw_.clear();
    for (auto& d : distance_) d.prepare(sampleRate);
    gain_.prepare(sampleRate, 0.0f);
    gain_.setTarget(gainTarget());
    modGain_.prepare(sampleRate, 1.0f);

    std::array<Position, 2> points{};
    for (std::size_t k = 0; k < static_cast<std::size_t>(numPoints()); ++k) {
        if (const auto* m = std::get_if<ManualMotion>(&motionSpec_[k].value)) {
            // The node smooths manual targets itself (SPEC §5.4 one-pole), so
            // the trajectory jumps to whatever it is given.
            ManualMotion jump = *m;
            jump.position = target_[k];
            jump.smoothMs = 0.0f;
            auto manual = std::make_unique<ManualTrajectory>(jump);
            manual_[k] = manual.get();
            motion_[k] = std::move(manual);
        } else {
            manual_[k] = nullptr;
            motion_[k] = MotionFactory::create(motionSpec_[k], &layout);
        }
        points[k] = motion_[k]->current();
    }
    if (numPoints() == 2) {
        end_ = points;
    } else if (stereo_) {
        const Position c = points[0];
        end_[0] = {Position::wrapAzimuth(c.azimuth - 0.5f * width_), c.elevation, c.distance};
        end_[1] = {Position::wrapAzimuth(c.azimuth + 0.5f * width_), c.elevation, c.distance};
    } else {
        end_[0] = end_[1] = points[0];
    }
    prevEnd_ = end_;
}

float SourceNode::gainTarget() const noexcept {
    return mute_ ? 0.0f : juce::Decibels::decibelsToGain(gainDb_, -200.0f);
}

void SourceNode::setGainDb(float gainDb, float rampMs) noexcept {
    gainDb_ = gainDb;
    gain_.setTarget(gainTarget(), rampMs);
}

void SourceNode::setMute(bool mute, float rampMs) noexcept {
    mute_ = mute;
    gain_.setTarget(gainTarget(), rampMs);
}

void SourceNode::setPosition(int channel, const Position& position, float smoothMs) noexcept {
    const Position p = position.normalised();
    for (int k = 0; k < 2; ++k) {
        if (channel >= 0 && channel != k) continue;
        const auto ks = static_cast<std::size_t>(k);
        target_[ks] = p;
        smoothMs_[ks] = smoothMs < 0.0f ? defaultSmoothMs_[ks] : smoothMs;
    }
}

void SourceNode::setSpread(float spread) noexcept {
    spread_ = std::clamp(spread, 0.0f, 1.0f);
}

void SourceNode::setMotionParam(int channel, MotionParam param, float value) noexcept {
    if (!std::isfinite(value)) return;
    for (int k = 0; k < 2; ++k) {
        if (channel >= 0 && channel != k) continue;
        const auto ks = static_cast<std::size_t>(k);
        if (const auto base = motionParamValue(motionSpec_[ks], param))
            paramOffsets_[ks][param] = value - *base;
    }
}

void SourceNode::resetMotion() noexcept {
    for (auto& m : motion_)
        if (m) m->reset();
    modulators_.reset();
}

void SourceNode::fadeOut(int numSamples) noexcept {
    mute_ = true;
    gain_.setTargetInSamples(0.0f, std::max(1, numSamples));
}

void SourceNode::fadeIn(int numSamples) noexcept {
    gain_.snapTo(0.0f);
    gain_.setTargetInSamples(gainTarget(), std::max(1, numSamples));
}

void SourceNode::restartMotion() noexcept {
    resetMotion();
}

void SourceNode::takeInputStateFrom(SourceNode& old) noexcept {
    if (input_ && old.input_) input_->takeStateFrom(*old.input_);
}

void SourceNode::takeStateFrom(SourceNode& old, bool sameLayout) noexcept {
    // Gain continues from where it is (no fade-in for a source that already
    // played), then ramps to this node's target.
    const float target = gainTarget();
    gain_.snapTo(old.gain_.current());
    gain_.setTarget(target);
    modGain_.snapTo(old.modGain_.current());

    smoothed_ = old.smoothed_;
    if (stereo_ == old.stereo_) {
        prevEnd_ = old.prevEnd_;
        end_ = old.end_;
    } else {
        prevEnd_[0] = prevEnd_[1] = old.prevEnd_[0];
    }
    if (juce::exactlyEqual(old.sampleRate_, sampleRate_)) distance_ = old.distance_;
    if (input_ && old.input_) input_->takeStateFrom(*old.input_);
    // Keep the running insert stage (plugin instances, delay lines) and move
    // it to this node's configuration; the fresh one goes back with `old`.
    std::swap(inserts_, old.inserts_);
    inserts_->retargetTo(*old.inserts_);

    // Motion phase (SPEC §5.2: sources keep their motion phase across swaps).
    const bool samePoints = numPoints() == old.numPoints();
    for (std::size_t k = 0; k < static_cast<std::size_t>(numPoints()); ++k) {
        const std::size_t ok = std::min(k, static_cast<std::size_t>(old.numPoints() - 1));
        if (!motion_[k] || !old.motion_[ok]) continue;
        if (motionSpec_[k].value.index() != old.motionSpec_[ok].value.index()) continue;  // new kind: phase 0
        const bool unchanged =
            specEqual(motionSpec_[k], old.motionSpec_[ok]) && (sameLayout || !usesLayout(motionSpec_[k]));
        if (unchanged && samePoints) {
            // Keep the running motion with all its state; the fresh one goes
            // back with the old graph.
            std::swap(motion_[k], old.motion_[ok]);
            std::swap(manual_[k], old.manual_[ok]);
        } else {
            motion_[k]->continueTimelineFrom(*old.motion_[ok]);
            // A live period edit keeps the position continuous (SPEC §8).
            const auto from = primaryPeriod(old.motionSpec_[ok]);
            const auto to = primaryPeriod(motionSpec_[k]);
            if (from && to) motion_[k]->reanchorPeriod(*from, *to);
        }
    }
    // Modulated insert parameters keep their base (same plugin instances).
    insertMods_ = old.insertMods_;
    if (specEqual(modulatorSpecs_, old.modulatorSpecs_))
        modulators_ = old.modulators_;
    else
        modulators_.continueTimelineFrom(old.modulators_);
}

void SourceNode::evaluatePositions(const MotionContext& ctx, const ModulatorBank::Output& mod,
                                   float width) noexcept {
    const double blockSeconds = static_cast<double>(ctx.numSamples) / sampleRate_;
    std::array<Position, 2> points{};
    for (std::size_t k = 0; k < static_cast<std::size_t>(numPoints()); ++k) {
        Trajectory& motion = *motion_[k];
        if (ManualTrajectory* manual = manual_[k]) {
            // One-pole smoothing towards the manual target, once per block in
            // Cartesian space (SPEC §2, §5.4).
            const Cartesian goal = target_[k].toCartesian();
            const float tau = smoothMs_[k] * 0.001f;
            if (tau <= 0.0f) {
                smoothed_[k] = goal;
            } else if (ctx.numSamples > 0) {
                const auto alpha =
                    static_cast<float>(1.0 - std::exp(-blockSeconds / static_cast<double>(tau)));
                smoothed_[k] = smoothed_[k] + (goal - smoothed_[k]) * alpha;
            }
            manual->setTarget(Position::fromCartesian(smoothed_[k]));
        }
        // `motionRight.*` drives the right point when it has its own spec;
        // otherwise both points share the `motion` spec and its paths.
        MotionModulation m = (k == 1 && hasMotionRight_) ? mod.motionRight : mod.motion;
        for (std::size_t i = 0; i < m.values.size(); ++i) m.values[i] += paramOffsets_[k].values[i];
        motion.setModulation(m);
        points[k] = motion.evaluate(ctx);
    }

    if (numPoints() == 2) {
        end_ = points;
    } else if (stereo_) {
        const Position c = points[0];
        end_[0] = {Position::wrapAzimuth(c.azimuth - 0.5f * width), c.elevation, c.distance};
        end_[1] = {Position::wrapAzimuth(c.azimuth + 0.5f * width), c.elevation, c.distance};
    } else {
        end_[0] = end_[1] = points[0];
    }
}

bool SourceNode::adoptLegacySpatializer(SourceNode& old) noexcept {
    if (kind_ != SpatializerKind::Ambisonic || old.kind_ != SpatializerKind::Ambisonic || !old.spatializer_)
        return false;
    std::swap(legacySpatializer_, old.spatializer_);
    return legacySpatializer_ != nullptr;
}

void SourceNode::process(const InputContext& ctx, const ClockState& clock, int n, SpeakerBus& bus,
                         SpeakerBus* legacyBus, const SourceNode* mirror) noexcept {
    const int channels = numChannels();
    float* ch[2] = {buffer_.getWritePointer(0), buffer_.getWritePointer(1)};

    if (mirror != nullptr) {
        // Ghost of a replaced file source: the same audio the new node plays.
        for (int c = 0; c < channels; ++c)
            std::copy(mirror->rawInput(std::min(c, mirror->numChannels() - 1)),
                      mirror->rawInput(std::min(c, mirror->numChannels() - 1)) + n, ch[c]);
    } else if (input_ && mirrorOf_ == 0) {
        input_->read(ctx, ch, channels, n);
    } else {
        for (int c = 0; c < channels; ++c) std::fill(ch[c], ch[c] + n, 0.0f);
    }
    if (keepRaw_)
        for (int c = 0; c < channels; ++c) std::copy(ch[c], ch[c] + n, raw_.getWritePointer(c));

    // Modulators first (SPEC §10), at the end of this block; insert
    // parameter targets apply to this block's plugin processing.
    const MotionContext mctx{clock, n, sampleRate_};
    const ModulatorBank::Output mod = modulators_.process(mctx);
    if (mod.numInserts > 0 ||
        std::any_of(insertMods_.begin(), insertMods_.end(), [](const InsertMod& m) { return m.active; }))
        applyInsertModulation(mod);

    // Insert slots (pre-gain, pre-spatializer; SPEC §5.3).
    inserts_->process(ch, channels, n);

    evaluatePositions(mctx, mod, width_ + mod.source[SourceParam::Width]);

    const float gainMod =
        modulatesGain_ ? juce::Decibels::decibelsToGain(mod.source[SourceParam::GainDb], -200.0f) : 1.0f;
    if (gain_.isSilent()) {
        // Muted / faded out: keep positions moving, skip the rendering.
        modGain_.snapTo(gainMod);
        prevEnd_ = end_;
        return;
    }
    if (modulatesGain_) modGain_.setTargetInSamples(gainMod, n);

    if (gain_.isRamping() || modGain_.isRamping()) {
        for (int i = 0; i < n; ++i) {
            const float g = gain_.next() * modGain_.next();
            for (int c = 0; c < channels; ++c) ch[c][i] *= g;
        }
    } else {
        const float g = gain_.current() * modGain_.current();
        if (!juce::exactlyEqual(g, 1.0f))
            for (int c = 0; c < channels; ++c) juce::FloatVectorOperations::multiply(ch[c], g, n);
    }

    const float spread = std::clamp(spread_ + mod.source[SourceParam::Spread], 0.0f, 1.0f);
    DistanceModel distance = distanceModel_;
    distance.k = std::max(0.0f, distance.k + mod.source[SourceParam::DistanceK]);
    distance.rMin = std::max(1e-3f, distance.rMin + mod.source[SourceParam::DistanceRMin]);
    distance.q = std::max(0.0f, distance.q + mod.source[SourceParam::DistanceQ]);

    for (int c = 0; c < channels; ++c) {
        const auto cs = static_cast<std::size_t>(c);
        // Direct ignores position (SPEC §6.4), so it also skips the distance model.
        if (kind_ != SpatializerKind::Direct)
            distance_[cs].process(ch[c], n, prevEnd_[cs].distance, end_[cs].distance, distance);
        float p = peak_[cs];
        for (int i = 0; i < n; ++i) p = std::max(p, std::abs(ch[c][i]));
        peak_[cs] = p;
        if (spatializer_) spatializer_->render(ch[c], n, prevEnd_[cs], end_[cs], spread, bus);
        if (legacyBus != nullptr && legacySpatializer_)
            legacySpatializer_->render(ch[c], n, prevEnd_[cs], end_[cs], spread, *legacyBus);
    }
    prevEnd_ = end_;
}

}  // namespace spacer
