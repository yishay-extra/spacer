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
        /// Motion parameters other than position have no engine command yet
        /// (SPEC §5.2 lists SetMotionParam, Command.h does not). Receives the
        /// request instead; return true if handled.
        std::function<bool(int sourceId, MotionParam param, float value)> motionParam;
        /// File transport (WP4). Return true if handled.
        std::function<bool(int sourceId, ev::SourceFile::Action action, double seconds)> fileTransport;
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

    void dispatch(const ControlEvent& event);

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
    Position currentPosition(int sourceId);
    void invalidateState();
    CueTick tick() const;

    const Project& project_;
    CueEngine* cues_;
    Config config_;
    Stats stats_;
    std::unordered_map<int, Position> positions_;
    std::unordered_map<int, DistanceModel> distances_;
};

}  // namespace spacer
