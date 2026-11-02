#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <memory>

#include "core/Layout.h"
#include "core/Motion.h"
#include "core/Source.h"
#include "core/Spatializer.h"
#include "core/spatial/DistanceModel.h"
#include "engine/GainRamp.h"
#include "engine/SourceInput.h"

namespace spacer {

/// One source in the graph (SPEC §5.3): input → [inserts, WP10] → gain ramp
/// → position (per block, interpolated by the spatializer) → distance model
/// → spatializer into the speaker bus. Stereo sources render two channels at
/// centre ± width/2 (or two independent points when `linkedPoints`).
///
/// Positions: until WP7 provides Motion evaluators, every source behaves as
/// Manual: it sits at its ManualMotion position (or, for other motion kinds,
/// at the motion's anchor: centre / from / first point) and follows
/// SetSourcePosition commands through a one-pole smoother (SPEC §5.4).
///
/// Constructed and prepared on the message thread; everything else is RT.
class SourceNode {
public:
    static constexpr float kDefaultPositionSmoothMs = 20.0f;

    SourceNode(const SourceSpec& spec, std::unique_ptr<Spatializer> spatializer,
               std::unique_ptr<SourceInput> input);

    /// Non-RT. Prepares spatializer and input, allocates buffers, and sets the
    /// gain ramp to start at 0 so a new source fades in.
    void prepare(const Layout& layout, double sampleRate, int maxBlockSize);

    int id() const noexcept { return id_; }
    int numChannels() const noexcept { return stereo_ ? 2 : 1; }
    SpatializerKind spatializerKind() const noexcept { return kind_; }
    const Spatializer* spatializer() const noexcept { return spatializer_.get(); }

    // ---- RT setters ----
    void setGainDb(float gainDb, float rampMs = -1.0f) noexcept;
    void setMute(bool mute, float rampMs = -1.0f) noexcept;
    /// channel: -1 = control point(s), 0 / 1 = left / right point (linkedPoints only).
    void setPosition(int channel, const Position& position, float smoothMs = -1.0f) noexcept;
    void setSpread(float spread) noexcept;
    void setWidth(float widthDeg) noexcept { width_ = widthDeg; }
    void setDistanceModel(const DistanceModel& model) noexcept { distanceModel_ = model; }

    /// RT. Ramp to silence over exactly `numSamples` (removed sources, §5.2).
    void fadeOut(int numSamples) noexcept;

    /// RT. Graph swap: continue from `old`'s state (current gain, positions,
    /// distance-filter state, input playback state) while heading for this
    /// node's own targets.
    void takeStateFrom(SourceNode& old) noexcept;

    /// RT. Render one block (numSamples <= maxBlockSize) into `bus`.
    void process(const InputContext& ctx, int numSamples, SpeakerBus& bus) noexcept;

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
    void advancePositions(int numSamples) noexcept;
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
    std::unique_ptr<Motion> motion_;  // WP7

    double sampleRate_{48000.0};
    juce::AudioBuffer<float> buffer_;
    GainRamp gain_;
    std::array<DistanceProcessor, 2> distance_;

    // Control points (1, or 2 for linkedPoints): target and smoothed value.
    std::array<Position, 2> target_{};
    std::array<Cartesian, 2> smoothed_{};
    std::array<float, 2> smoothMs_{kDefaultPositionSmoothMs, kDefaultPositionSmoothMs};
    float defaultSmoothMs_{kDefaultPositionSmoothMs};

    // Per rendered channel: position at the end of the previous / this block.
    std::array<Position, 2> prevEnd_{};
    std::array<Position, 2> end_{};
    std::array<float, 2> peak_{};
};

}  // namespace spacer
