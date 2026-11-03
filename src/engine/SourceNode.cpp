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
      modulatorSpecs_(spec.modulators) {
    motionSpec_[0] = spec.motion;
    motionSpec_[1] = (linkedPoints_ && stereo_ && spec.motionRight) ? *spec.motionRight : spec.motion;
    for (std::size_t k = 0; k < 2; ++k) {
        target_[k] = manualPositionOf(motionSpec_[k]).normalised();
        defaultSmoothMs_[k] = smoothMs_[k] = smoothMsOf(motionSpec_[k]);
        smoothed_[k] = target_[k].toCartesian();
    }
    modulators_.configure(modulatorSpecs_);
    for (std::size_t i = 0; i < std::min<std::size_t>(modulatorSpecs_.size(), kMaxModulators); ++i) {
        const auto path = ParameterPath::parse(modulatorSpecs_[i].target);
        if (path && path->domain == ParameterPath::Domain::Source && path->source == SourceParam::GainDb)
            modulatesGain_ = true;
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
        }
    }
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
        MotionModulation m = mod.motion;
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

void SourceNode::process(const InputContext& ctx, const ClockState& clock, int n, SpeakerBus& bus) noexcept {
    const int channels = numChannels();
    float* ch[2] = {buffer_.getWritePointer(0), buffer_.getWritePointer(1)};

    if (input_)
        input_->read(ctx, ch, channels, n);
    else
        for (int c = 0; c < channels; ++c) std::fill(ch[c], ch[c] + n, 0.0f);

    // Insert slots (pre-gain, pre-spatializer; SPEC §5.3).
    inserts_->process(ch, channels, n);

    // Modulators first, then the motion (SPEC §10), both at the end of this block.
    const MotionContext mctx{clock, n, sampleRate_};
    const ModulatorBank::Output mod = modulators_.process(mctx);
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
    }
    prevEnd_ = end_;
}

}  // namespace spacer
