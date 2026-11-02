#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <memory>
#include <vector>

#include "core/Clock.h"
#include "core/Layout.h"
#include "core/Motion.h"
#include "core/Source.h"
#include "core/Spatializer.h"
#include "core/motion/ManualTrajectory.h"
#include "core/motion/ModulatorBank.h"
#include "core/motion/Trajectory.h"
#include "core/spatial/DistanceModel.h"
#include "engine/GainRamp.h"
#include "engine/SourceInput.h"

namespace spacer {

/// One source in the graph (SPEC §5.3): input → [inserts, WP10] → gain ramp
/// → position (per block, interpolated by the spatializer) → distance model
/// → spatializer into the speaker bus. Stereo sources render two channels at
/// centre ± width/2 (or two independent points when `linkedPoints`).
///
/// Motion (SPEC §10): each control point (1, or 2 for linkedPoints) has a
/// Trajectory from MotionFactory, evaluated once per block from the clock,
/// after the source's ModulatorBank has applied its offsets. Manual points
/// follow SetSourcePosition through the one-pole smoother of SPEC §5.4
/// (smoothMs from the ManualMotion, 20 ms default); modulation of
/// motion.position.* is added after the smoother. For other kinds,
/// SetSourcePosition is ignored (the motion owns the position; the UI moves
/// its centre with SetMotionParam). Source-level modulation (gain, spread,
/// width, distance.*) is added to the node's values each block; a gain
/// modulation is ramped across the block.
///
/// Constructed and prepared on the message thread; everything else is RT.
class SourceNode {
public:
    static constexpr float kDefaultPositionSmoothMs = 20.0f;

    SourceNode(const SourceSpec& spec, std::unique_ptr<Spatializer> spatializer,
               std::unique_ptr<SourceInput> input);

    /// Non-RT. Prepares spatializer, input and motions, allocates buffers,
    /// and sets the gain ramp to start at 0 so a new source fades in.
    void prepare(const Layout& layout, double sampleRate, int maxBlockSize);

    int id() const noexcept { return id_; }
    int numChannels() const noexcept { return stereo_ ? 2 : 1; }
    SpatializerKind spatializerKind() const noexcept { return kind_; }
    const Spatializer* spatializer() const noexcept { return spatializer_.get(); }
    SourceInput* input() noexcept { return input_.get(); }
    const Trajectory* motion(int point) const noexcept {
        return motion_[static_cast<std::size_t>(point)].get();
    }
    const ModulatorBank& modulators() const noexcept { return modulators_; }

    // ---- RT setters ----
    void setGainDb(float gainDb, float rampMs = -1.0f) noexcept;
    void setMute(bool mute, float rampMs = -1.0f) noexcept;
    /// channel: -1 = control point(s), 0 / 1 = left / right point (linkedPoints only).
    void setPosition(int channel, const Position& position, float smoothMs = -1.0f) noexcept;
    void setSpread(float spread) noexcept;
    void setWidth(float widthDeg) noexcept { width_ = widthDeg; }
    void setDistanceModel(const DistanceModel& model) noexcept { distanceModel_ = model; }
    /// Absolute live value of a motion parameter (see cmd::SetMotionParam).
    void setMotionParam(int channel, MotionParam param, float value) noexcept;
    /// Phase 0 at the next block for the motion(s) and modulator LFOs.
    void resetMotion() noexcept;
    void setModulatorValue(int index, float value) noexcept { modulators_.setExternal(index, value); }

    /// RT. Ramp to silence over exactly `numSamples` (removed sources, §5.2).
    void fadeOut(int numSamples) noexcept;

    /// RT. Graph swap: continue from `old`'s state (current gain, positions,
    /// distance-filter state, input playback state, motion phase) while
    /// heading for this node's own targets. Motions: an unchanged spec keeps
    /// the running motion (all its state); a changed spec of the same kind
    /// keeps the phase (time origin); another kind starts at phase 0 (SPEC
    /// §10). `sameLayout` tells whether speaker targets resolve the same.
    void takeStateFrom(SourceNode& old, bool sameLayout) noexcept;

    /// RT. Render one block (numSamples <= maxBlockSize) into `bus`. `clock`
    /// is the state at the start of the block.
    void process(const InputContext& ctx, const ClockState& clock, int numSamples, SpeakerBus& bus) noexcept;

    // ---- feedback (audio thread) ----
    float takePeak(int channel) noexcept {
        const float p = peak_[static_cast<std::size_t>(channel)];
        peak_[static_cast<std::size_t>(channel)] = 0.0f;
        return p;
    }
    Position renderedPosition(int channel) const noexcept {
        return prevEnd_[static_cast<std::size_t>(channel)];
    }
    float currentGain() const noexcept { return gain_.current(); }

private:
    float gainTarget() const noexcept;
    void evaluatePositions(const MotionContext& ctx, const ModulatorBank::Output& mod, float width) noexcept;
    int numPoints() const noexcept { return stereo_ && linkedPoints_ ? 2 : 1; }

    int id_{0};
    bool stereo_{false};
    bool linkedPoints_{false};
    SpatializerKind kind_{SpatializerKind::Vbap};

    float gainDb_{0.0f};
    bool mute_{false};
    float spread_{0.0f};
    float width_{60.0f};
    DistanceModel distanceModel_;

    std::unique_ptr<Spatializer> spatializer_;
    std::unique_ptr<SourceInput> input_;

    // Motion per control point.
    std::array<MotionSpec, 2> motionSpec_{};
    std::array<std::unique_ptr<Trajectory>, 2> motion_{};
    std::array<ManualTrajectory*, 2> manual_{};       // = motion_[k] when Manual
    std::array<MotionModulation, 2> paramOffsets_{};  // SetMotionParam
    std::vector<ModulatorSpec> modulatorSpecs_;
    ModulatorBank modulators_;
    bool modulatesGain_{false};

    double sampleRate_{48000.0};
    juce::AudioBuffer<float> buffer_;
    GainRamp gain_;
    GainRamp modGain_;  // gain modulation, ramped per block
    std::array<DistanceProcessor, 2> distance_;

    // Manual control points: target and smoothed value.
    std::array<Position, 2> target_{};
    std::array<Cartesian, 2> smoothed_{};
    std::array<float, 2> smoothMs_{kDefaultPositionSmoothMs, kDefaultPositionSmoothMs};
    std::array<float, 2> defaultSmoothMs_{kDefaultPositionSmoothMs, kDefaultPositionSmoothMs};

    // Per rendered channel: position at the end of the previous / this block.
    std::array<Position, 2> prevEnd_{};
    std::array<Position, 2> end_{};
    std::array<float, 2> peak_{};
};

}  // namespace spacer
