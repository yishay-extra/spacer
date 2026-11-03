#pragma once

// Message-thread control events (SPEC §5.2, §12). OSC and MIDI are parsed on
// their own threads into these small value types, handed to the message
// thread, and only there turned into engine Commands / cue-engine calls by
// ControlDispatcher. Nothing in here touches the audio command queue.

#include <string>
#include <variant>

#include "core/InsertParamPath.h"
#include "core/Position.h"

namespace spacer {

/// A source or speaker named by integer id or by name (SPEC §12.1 `{id}`).
struct EntityRef {
    int id{0};         // used when name is empty
    std::string name;  // non-empty: look up by name
    bool byName() const noexcept { return !name.empty(); }
    bool operator==(const EntityRef&) const = default;
};

namespace ev {

/// Any numeric source parameter addressed by a ParameterPath string
/// (SPEC §10: "gain", "spread", "distance.k", "motion.radius",
/// "motion.position.az", …). OSC `/az`, `/el`, `/dist` use the
/// `motion.position.*` paths.
struct SetSourceParam {
    EntityRef source;
    std::string path;
    float value{0.0f};
    bool operator==(const SetSourceParam&) const = default;
};
struct SetSourcePositionAed {
    EntityRef source;
    Position position;
    bool operator==(const SetSourcePositionAed&) const = default;
};
struct SetSourcePositionXyz {
    EntityRef source;
    Cartesian xyz;
    bool operator==(const SetSourcePositionXyz&) const = default;
};
struct SetSourceMute {
    EntityRef source;
    bool mute{false};
    bool operator==(const SetSourceMute&) const = default;
};
struct SourceFile {
    enum class Action { Play, Stop, Seek };
    EntityRef source;
    Action action{Action::Play};
    double seconds{0.0};  // Seek
    bool operator==(const SourceFile&) const = default;
};

struct SetSpeakerTrim {
    EntityRef speaker;
    float trimDb{0.0f};
    bool operator==(const SetSpeakerTrim&) const = default;
};
struct SetSpeakerMute {
    EntityRef speaker;
    bool mute{false};
    bool operator==(const SetSpeakerMute&) const = default;
};
/// Insert slot parameter (SPEC §13 `source.{id}.insert.{slot}.param.{index}`,
/// and the speaker form), normalised 0..1. Source id 0 = the UI-selected source.
struct SetInsertParam {
    InsertOwner owner{InsertOwner::Source};
    EntityRef target;
    int slot{0};
    int index{0};
    float value{0.0f};
    bool operator==(const SetInsertParam&) const = default;
};
struct SetMasterGain {
    float gainDb{0.0f};
    bool operator==(const SetMasterGain&) const = default;
};
struct Panic {
    bool engaged{true};
    bool operator==(const Panic&) const = default;
};

struct CueGo {
    bool operator==(const CueGo&) const = default;
};
struct CueBack {
    bool operator==(const CueBack&) const = default;
};
/// By index (name empty) or by cue name.
struct CueJump {
    int index{0};
    std::string name;
    bool operator==(const CueJump&) const = default;
};
struct RecallScene {
    std::string name;
    float seconds{0.0f};
    bool operator==(const RecallScene&) const = default;
};
/// MIDI program change (SPEC §12.2): scene index, 0-based.
struct RecallSceneIndex {
    int index{0};
    float seconds{0.0f};
    bool operator==(const RecallSceneIndex&) const = default;
};

struct ClockBpm {
    double bpm{120.0};
    bool operator==(const ClockBpm&) const = default;
};
struct ClockPlay {
    bool operator==(const ClockPlay&) const = default;
};
struct ClockStop {
    bool operator==(const ClockStop&) const = default;
};
struct ClockContinue {
    bool operator==(const ClockContinue&) const = default;
};
struct ClockBeat {
    double beat{0.0};
    bool operator==(const ClockBeat&) const = default;
};

/// An OSC message whose address is not part of the SPEC §12.1 address space:
/// offered to cues with an `Osc{address}` trigger, otherwise counted unknown.
struct OscTrigger {
    std::string address;
    /// The message's first number, if any (for `osc{address}` modulators).
    float value{0.0f};
    bool hasValue{false};
    bool operator==(const OscTrigger&) const = default;
};
/// A MIDI note-on (or CC with value > 0) offered to cues with a `Midi` trigger.
struct MidiTrigger {
    enum class Type { Note, Cc };
    Type type{Type::Note};
    int number{0};
    int channel{1};  // 1..16
    bool operator==(const MidiTrigger&) const = default;
};

}  // namespace ev

using ControlEvent =
    std::variant<ev::SetSourceParam, ev::SetSourcePositionAed, ev::SetSourcePositionXyz, ev::SetSourceMute,
                 ev::SourceFile, ev::SetSpeakerTrim, ev::SetSpeakerMute, ev::SetInsertParam,
                 ev::SetMasterGain, ev::Panic, ev::CueGo, ev::CueBack, ev::CueJump, ev::RecallScene,
                 ev::RecallSceneIndex, ev::ClockBpm, ev::ClockPlay, ev::ClockStop, ev::ClockContinue,
                 ev::ClockBeat, ev::OscTrigger, ev::MidiTrigger>;

}  // namespace spacer
