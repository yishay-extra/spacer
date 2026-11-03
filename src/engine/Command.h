#pragma once

// Control → audio commands (SPEC §5.2). Every alternative is a small POD so
// the whole variant is trivially copyable and travels through SpscQueue by
// value. Ids are model ids (SourceSpec::id, Speaker::id), not indices.
//
// A negative ramp / smoothing time means "use the engine default" (10 ms for
// gains, SPEC §5.4; the source's ManualMotion::smoothMs for positions).

#include <type_traits>
#include <variant>

#include "core/InsertParamPath.h"
#include "core/Position.h"
#include "core/Source.h"
#include "core/SpeakerChain.h"
#include "core/motion/MotionParam.h"

namespace spacer {

class Graph;
struct RecallPlan;

namespace cmd {

struct SetSourceGain {
    int sourceId{0};
    float gainDb{0.0f};
    float rampMs{-1.0f};
};
struct SetSourceMute {
    int sourceId{0};
    bool mute{false};
    float rampMs{-1.0f};
};
/// Manual position. `channel` -1 = the source's control point (centre of a
/// stereo pair, or both points of a linkedPoints source); 0 / 1 = left / right
/// point of a linkedPoints source.
struct SetSourcePosition {
    int sourceId{0};
    int channel{-1};
    Position position;
    float smoothMs{-1.0f};
};
struct SetSourceSpread {
    int sourceId{0};
    float spread{0.0f};
};
struct SetSourceWidth {
    int sourceId{0};
    float widthDeg{60.0f};
};
struct SetSourceDistance {
    int sourceId{0};
    DistanceModel model;
};

/// Live absolute value of a motion parameter (SPEC §5.2 `SetMotionParam`,
/// OSC `/source/{id}/motion/{param}`), in the parameter's units. Applied as
/// an offset over the source's motion spec, so the phase is not restarted;
/// parameters the motion kind does not have are ignored. `channel` as in
/// SetSourcePosition. Not carried across SwapGraph: the control side writes
/// the value into the model it builds the next graph from.
struct SetMotionParam {
    int sourceId{0};
    int channel{-1};
    MotionParam param{MotionParam::Radius};
    float value{0.0f};
};
/// Restart the source's motion (and modulator LFO) phase at the next block
/// (SPEC §10 `reset()`, cue `restartMotionPhase`).
struct ResetMotion {
    int sourceId{0};
};
/// Value of an external (osc / midi) modulator source (SPEC §10): OSC values
/// as received, MIDI CC normalised to 0..1 by the sender.
struct SetModulatorValue {
    int sourceId{0};
    int index{0};
    float value{0.0f};
};

/// File transport (SPEC §8, OSC `/source/{id}/file/play | stop | seek`).
/// `play` at the end of a non-looping file restarts it from `startSeconds`.
struct FilePlay {
    int sourceId{0};
};
struct FileStop {
    int sourceId{0};
};
/// Seek to `seconds` of file time; playback continues if it was playing.
struct FileSeek {
    int sourceId{0};
    double seconds{0.0};
};

struct SetSpeakerTrim {
    int speakerId{0};
    float trimDb{0.0f};
    float rampMs{-1.0f};
};
struct SetSpeakerMute {
    int speakerId{0};
    bool mute{false};
    float rampMs{-1.0f};
};
/// Crossfades from the old to the new delay over 50 ms (SPEC §5.4).
struct SetSpeakerDelay {
    int speakerId{0};
    float userDelayMs{0.0f};
};
struct SetSpeakerPolarity {
    int speakerId{0};
    bool inverted{false};
};
struct SetSpeakerEq {
    int speakerId{0};
    Eq3 eq;
};

/// Insert slot parameter (SPEC §13 parameter paths), normalised 0..1.
struct SetInsertParam {
    InsertOwner owner{InsertOwner::Source};
    int id{0};  // source or speaker id
    int slot{0};
    int index{0};
    float value{0.0f};
};
/// Bypass an insert slot (crossfaded, latency-aligned).
struct SetInsertBypass {
    InsertOwner owner{InsertOwner::Source};
    int id{0};
    int slot{0};
    bool bypassed{false};
};

struct SetMasterGain {
    float gainDb{0.0f};
    float rampMs{-1.0f};
};
/// Panic: ramps every output to silence (and back when released).
struct SetPanic {
    bool engaged{true};
};

struct ClockPlay {};
struct ClockStop {};
struct ClockContinue {};
struct ClockSetBpm {
    double bpm{120.0};
};
struct ClockSetBeat {
    double beat{0.0};
};
/// Tap at the engine time of the block in which the command is applied.
struct ClockTap {};

/// Structural change: replace the whole graph. Ownership of `graph` passes to
/// the engine; the old graph comes back through EngineProcessor's retire
/// queue for deletion on the message thread.
struct SwapGraph {
    Graph* graph{nullptr};
};

/// Scene recall (SPEC §11). Ownership of `plan` passes to the engine.
struct RecallScene {
    RecallPlan* plan{nullptr};
};
/// Drops a scheduled (AtBeat) recall that has not fired yet.
struct CancelScheduledRecall {};

}  // namespace cmd

using Command =
    std::variant<cmd::SetSourceGain, cmd::SetSourceMute, cmd::SetSourcePosition, cmd::SetSourceSpread,
                 cmd::SetSourceWidth, cmd::SetSourceDistance, cmd::SetMotionParam, cmd::ResetMotion,
                 cmd::SetModulatorValue, cmd::FilePlay, cmd::FileStop, cmd::FileSeek, cmd::SetSpeakerTrim,
                 cmd::SetSpeakerMute, cmd::SetSpeakerDelay, cmd::SetSpeakerPolarity, cmd::SetSpeakerEq,
                 cmd::SetInsertParam, cmd::SetInsertBypass, cmd::SetMasterGain, cmd::SetPanic, cmd::ClockPlay,
                 cmd::ClockStop, cmd::ClockContinue, cmd::ClockSetBpm, cmd::ClockSetBeat, cmd::ClockTap,
                 cmd::SwapGraph, cmd::RecallScene, cmd::CancelScheduledRecall>;

static_assert(std::is_trivially_copyable_v<Command>);

}  // namespace spacer
