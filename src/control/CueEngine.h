#pragma once

#include <optional>
#include <string>

#include "core/CueSequencer.h"
#include "core/Project.h"
#include "engine/EngineProcessor.h"
#include "engine/GraphBuilder.h"

namespace spacer {

/// Message-thread cue engine (SPEC §5.1, §11): owns the armed cue list's
/// state machine and turns fired cues into cmd::RecallScene on the engine's
/// command queue. Like all control code it is called from the message thread
/// only (OSC / MIDI / UI callbacks hand events over first).
///
/// AfterPrevious, TimeOfDay, Osc, Midi and Manual cues are evaluated here
/// (message-thread timing, ~tick rate). AtBeat is handed to the audio thread
/// as a scheduled recall so it fires on the exact sample; the engine reports
/// it with fb::CueFired, which the app forwards to onEngineCueFired().
///
/// The project is the live model: when a cue fires, its scene is applied to
/// `project` (sources / speaker chains / master), which is also the "current
/// scene" of the next recall's diff.
class CueEngine {
public:
    struct Config {
        double sampleRate{48000.0};
        int maxBlockSize{128};
        /// Inputs for sources a recall adds (same factory as the live graph).
        GraphBuilder::InputFactory inputFactory;
        int numOutputChannels{-1};
        std::uint64_t randomSeed{1};
    };

    CueEngine(EngineProcessor& engine, Project& project, Config config);

    /// Arms the named list (only one list is armed at a time). False if no such list.
    bool arm(const std::string& cueListName, const CueTick& tick);
    /// Arms the first list. False if the project has none.
    bool armFirst(const CueTick& tick);
    bool armed() const noexcept { return armed_; }
    const CueSequencer& sequencer() const noexcept { return sequencer_; }

    // Transport. Each returns true if a cue fired.
    bool go(const CueTick& tick);
    bool back(const CueTick& tick);
    bool jump(int cueIndex, const CueTick& tick);

    /// Call regularly (e.g. 30 Hz) with the latest known clock: AfterPrevious, TimeOfDay.
    bool tick(const CueTick& tick);
    bool onOsc(const std::string& address, const CueTick& tick);
    bool onMidi(trigger::Midi::Type type, int number, int channel, const CueTick& tick);
    /// fb::CueFired{token} from the engine's feedback.
    bool onEngineCueFired(int token, const CueTick& tick);

    /// Scene manager: recall a scene outside any cue list. False if the scene
    /// does not exist or the command queue was full.
    bool recallScene(const std::string& sceneName, float transitionSeconds, bool restartMotionPhase);

    /// Scene manager: store the live state under `name` (replacing a scene of
    /// that name), "Update scene".
    void storeScene(const std::string& name);

    int scheduledCommandsDropped() const noexcept { return dropped_; }
    /// Scene names referenced by cues that do not exist (the cue still advances the list).
    const std::string& lastError() const noexcept { return lastError_; }

private:
    bool fire(int cueIndex, const CueTick& tick);
    bool recall(const Scene& scene, float seconds, bool restart, double atBeat, int token);
    void scheduleBeatTrigger(const CueTick& tick);

    EngineProcessor& engine_;
    Project& project_;
    Config config_;
    CueSequencer sequencer_;
    bool armed_{false};
    bool beatScheduled_{false};
    int dropped_{0};
    std::string lastError_;
};

}  // namespace spacer
