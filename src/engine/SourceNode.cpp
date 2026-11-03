#include "engine/SourceNode.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <variant>

namespace spacer {

namespace {

/// Where a source sits before WP7 evaluates its motion.
Position anchorOf(const MotionSpec& spec) {
    return std::visit(
        [](const auto& m) -> Position {
            using M = std::decay_t<decltype(m)>;
            if constexpr (std::is_same_v<M, ManualMotion>) {
                return m.position;
            } else if constexpr (std::is_same_v<M, OrbitMotion> || std::is_same_v<M, LissajousMotion>) {
                return m.centre;
            } else if constexpr (std::is_same_v<M, LineMotion>) {
                return m.from;
            } else if constexpr (std::is_same_v<M, PathMotion>) {
                return m.points.empty() ? Position{} : Position::fromCartesian(m.points.front());
            } else if constexpr (std::is_same_v<M, StepSequenceMotion>) {
                if (!m.steps.empty())
                    if (const auto* p = std::get_if<Position>(&m.steps.front().target)) return *p;
                return Position{};
            } else {
                return Position{};
            }
        },
        spec.value);
}

float smoothMsOf(const MotionSpec& spec) {
    if (const auto* m = std::get_if<ManualMotion>(&spec.value)) return m->smoothMs;
    return SourceNode::kDefaultPositionSmoothMs;
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
      input_(std::move(input)) {
    target_[0] = anchorOf(spec.motion);
    defaultSmoothMs_ = smoothMsOf(spec.motion);
    smoothMs_[0] = defaultSmoothMs_;
    if (linkedPoints_ && stereo_) {
        const MotionSpec& right = spec.motionRight ? *spec.motionRight : spec.motion;
        target_[1] = anchorOf(right);
        smoothMs_[1] = smoothMsOf(right);
    } else {
        target_[1] = target_[0];
        smoothMs_[1] = smoothMs_[0];
    }
    for (std::size_t k = 0; k < 2; ++k) smoothed_[k] = target_[k].toCartesian();
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
    advancePositions(0);
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
    const float ms = smoothMs < 0.0f ? defaultSmoothMs_ : smoothMs;
    for (int k = 0; k < 2; ++k) {
        if (channel >= 0 && channel != k) continue;
        target_[static_cast<std::size_t>(k)] = p;
        smoothMs_[static_cast<std::size_t>(k)] = ms;
    }
}

void SourceNode::setSpread(float spread) noexcept {
    spread_ = std::clamp(spread, 0.0f, 1.0f);
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
    if (motion_) motion_->reset();
}

void SourceNode::takeInputStateFrom(SourceNode& old) noexcept {
    if (input_ && old.input_) input_->takeStateFrom(*old.input_);
}

void SourceNode::takeStateFrom(SourceNode& old) noexcept {
    // Gain continues from where it is (no fade-in for a source that already
    // played), then ramps to this node's target.
    const float target = gainTarget();
    gain_.snapTo(old.gain_.current());
    gain_.setTarget(target);

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
    // Motion phase carry-over: WP7.
}

void SourceNode::advancePositions(int numSamples) noexcept {
    // One-pole smoothing towards the manual target, evaluated once per block
    // in Cartesian space (SPEC §2, §5.4).
    const double blockSeconds = static_cast<double>(numSamples) / sampleRate_;
    for (std::size_t k = 0; k < static_cast<std::size_t>(numPoints()); ++k) {
        const Cartesian goal = target_[k].toCartesian();
        const float tau = smoothMs_[k] * 0.001f;
        if (tau <= 0.0f || numSamples == 0) {
            if (tau <= 0.0f) smoothed_[k] = goal;
            continue;
        }
        const auto alpha = static_cast<float>(1.0 - std::exp(-blockSeconds / static_cast<double>(tau)));
        smoothed_[k] = smoothed_[k] + (goal - smoothed_[k]) * alpha;
    }

    if (numPoints() == 2) {
        end_[0] = Position::fromCartesian(smoothed_[0]);
        end_[1] = Position::fromCartesian(smoothed_[1]);
    } else if (stereo_) {
        const Position centre = Position::fromCartesian(smoothed_[0]);
        end_[0] = {Position::wrapAzimuth(centre.azimuth - 0.5f * width_), centre.elevation, centre.distance};
        end_[1] = {Position::wrapAzimuth(centre.azimuth + 0.5f * width_), centre.elevation, centre.distance};
    } else {
        end_[0] = end_[1] = Position::fromCartesian(smoothed_[0]);
    }
}

void SourceNode::process(const InputContext& ctx, int n, SpeakerBus& bus) noexcept {
    const int channels = numChannels();
    float* ch[2] = {buffer_.getWritePointer(0), buffer_.getWritePointer(1)};

    if (input_)
        input_->read(ctx, ch, channels, n);
    else
        for (int c = 0; c < channels; ++c) std::fill(ch[c], ch[c] + n, 0.0f);

    // Insert slots (pre-gain, pre-spatializer; SPEC §5.3).
    inserts_->process(ch, channels, n);

    advancePositions(n);

    if (gain_.isSilent()) {
        // Muted / faded out: keep positions moving, skip the rendering.
        prevEnd_ = end_;
        return;
    }

    if (gain_.isRamping()) {
        if (channels == 2) {
            for (int i = 0; i < n; ++i) {
                const float g = gain_.next();
                ch[0][i] *= g;
                ch[1][i] *= g;
            }
        } else {
            gain_.apply(ch[0], n);
        }
    } else {
        const float g = gain_.current();
        if (!juce::exactlyEqual(g, 1.0f))
            for (int c = 0; c < channels; ++c)
                for (int i = 0; i < n; ++i) ch[c][i] *= g;
    }

    for (int c = 0; c < channels; ++c) {
        const auto cs = static_cast<std::size_t>(c);
        // Direct ignores position (SPEC §6.4), so it also skips the distance model.
        if (kind_ != SpatializerKind::Direct)
            distance_[cs].process(ch[c], n, prevEnd_[cs].distance, end_[cs].distance, distanceModel_);
        float p = peak_[cs];
        for (int i = 0; i < n; ++i) p = std::max(p, std::abs(ch[c][i]));
        peak_[cs] = p;
        if (spatializer_) spatializer_->render(ch[c], n, prevEnd_[cs], end_[cs], spread_, bus);
    }
    prevEnd_ = end_;
}

}  // namespace spacer
