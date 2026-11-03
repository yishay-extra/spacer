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
                 cmd::SetSourceWidth, cmd::SetSourceDistance, cmd::SetSpeakerTrim, cmd::SetSpeakerMute,
                 cmd::SetSpeakerDelay, cmd::SetSpeakerPolarity, cmd::SetSpeakerEq, cmd::SetInsertParam,
                 cmd::SetInsertBypass, cmd::SetMasterGain, cmd::SetPanic, cmd::ClockPlay, cmd::ClockStop,
                 cmd::ClockContinue, cmd::ClockSetBpm, cmd::ClockSetBeat, cmd::ClockTap, cmd::SwapGraph,
                 cmd::RecallScene, cmd::CancelScheduledRecall>;

static_assert(std::is_trivially_copyable_v<Command>);

}  // namespace spacer
