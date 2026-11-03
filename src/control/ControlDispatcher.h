#pragma once

// ControlEvent → engine Command / cue-engine call (SPEC §5.2, §12).
// Message thread only: this is the single producer of the audio command queue
// for everything that comes in over OSC and MIDI.

#include <cstdint>
#include <functional>
#include <unordered_map>

#include "control/ControlEvent.h"
#include "control/CueEngine.h"
#include "core/CueSequencer.h"
#include "core/Project.h"
#include "core/motion/MotionParam.h"
#include "engine/Command.h"

namespace spacer {

class ControlDispatcher {
public:
    struct Config {
        /// Pushes to the engine's command queue (EngineProcessor::push). The
        /// owner guarantees it is only ever called on the message thread.
        std::function<bool(const Command&)> push;
        /// "Now" for cue triggers. Required for any cue / scene event.
        std::function<CueTick()> makeTick;
        /// Source a bare ParameterPath (no `source.<id>.` scope) applies to; 0 / unset = none.
        std::function<int()> selectedSource;
        /// Optional override for motion parameters other than position. Unset
        /// (the default), they become cmd::SetMotionParam (both control points
        /// of a linkedPoints source). Return true if handled.
        std::function<bool(int sourceId, MotionParam param, float value)> motionParam;
        /// Optional override for file transport. Unset (the default), it
        /// becomes cmd::FilePlay / FileStop / FileSeek. Return true if handled.
        std::function<bool(int sourceId, ev::SourceFile::Action action, double seconds)> fileTransport;
        /// Timed push (OSC bundle timetags, SPEC §12.1 "scheduled to the
        /// sample"): `dueEpochSeconds` is the timetag (seconds since 1970).
        /// Unset, timed events are pushed immediately through `push`.
        std::function<bool(const Command&, double dueEpochSeconds)> pushAt;
    };
    struct Stats {
        std::uint64_t dispatched{0};
        std::uint64_t unknownAddress{0};    // OSC address nobody (incl. cues) wanted
        std::uint64_t unresolvedTarget{0};  // unknown source / speaker / scene / cue
        std::uint64_t badPath{0};           // not a ParameterPath
        std::uint64_t unsupported{0};       // no engine support yet
        std::uint64_t commandsDropped{0};   // queue full
    };

    /// `cues` may be null (cue / scene events are then counted unsupported).
    ControlDispatcher(const Project& project, CueEngine* cues, Config config);

    /// `dueEpochSeconds` > 0: the event carries an OSC timetag. Engine
    /// commands it produces go through Config::pushAt with that time; cue and
    /// scene events ignore it (the caller delivers those when due).
    void dispatch(const ControlEvent& event, double dueEpochSeconds = 0.0);

    /// A MIDI CC (any channel) for `midi{cc}` modulator sources (SPEC §10):
    /// every modulator listening to `cc` gets value / 127. True if any did.
    bool modulatorCc(int cc, int value);

    /// Latest rendered position of a source (from fb::SourcePosition), used
    /// for single-component moves (`/az`, `/el`, `/dist`) of non-manual sources.
    void observePosition(int sourceId, const Position& position);
    Stats stats() const noexcept { return stats_; }

private:
    struct Visitor;
    bool send(const Command& c);
    int resolveSource(const EntityRef& ref);
    int resolveSpeaker(const EntityRef& ref);
    bool setSourceParam(const EntityRef& ref, const std::string& path, float value);
    bool setPosition(int sourceId, const Position& p);
    /// `osc{address}` modulator sources: SetModulatorValue for each match.
    bool modulatorOsc(const std::string& address, float value);
    Position currentPosition(int sourceId);
    void invalidateState();
    CueTick tick() const;

    const Project& project_;
    CueEngine* cues_;
    Config config_;
    Stats stats_;
    std::unordered_map<int, Position> positions_;
    std::unordered_map<int, DistanceModel> distances_;
    double due_{0.0};  // timetag of the event being dispatched (0 = now)
};

}  // namespace spacer
