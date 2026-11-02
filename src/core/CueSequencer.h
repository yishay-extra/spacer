#pragma once

// Cue list state machine (SPEC §11): current / next, Go / Back / Jump, loop,
// and trigger evaluation. Pure logic, no threads, no engine: callers pass in
// the time and get back the index of the cue that fired, then do the recall.
//
// Rules the spec leaves open (decided here, listed in the WP9 PR):
//  - Go fires the *next* cue whatever its trigger is (it is the manual
//    override); before the first cue has fired, the next cue is cue 0.
//  - A cue's trigger is only live while it is the next cue, so Osc / Midi /
//    TimeOfDay / AtBeat / AfterPrevious are all "how the list advances".
//    Jump and Back fire cues directly.
//  - Back fires the previous cue (wrapping to the last cue when `loop` and the
//    current cue is the first). From "nothing fired yet" it does nothing.
//  - Past the last cue Go does nothing, or wraps to cue 0 when `loop` is set.
//  - AfterPrevious counts from the moment the previous cue *started* (or from
//    arm() for the first cue). A random extra in [0, randomRange] (same unit)
//    is drawn each time the cue becomes next, from a seeded generator.
//  - `postWaitSeconds` of a cue delays an AfterPrevious successor: it fires
//    when both its own delay and the previous cue's post-wait have elapsed.
//  - TimeOfDay fires once per matching minute.
//  - AtBeat is not evaluated here (sample accuracy needs the audio thread):
//    pendingBeatTrigger() tells the caller what to schedule in the engine and
//    beatFired() tells the sequencer the engine did it.

#include <cstdint>
#include <optional>
#include <string>

#include "core/Clock.h"
#include "core/Cue.h"
#include "core/motion/Rng.h"

namespace spacer {

/// Local wall-clock time, for TimeOfDay triggers.
struct WallClock {
    long long minuteStamp{0};  // minutes since some epoch; changes every minute, never repeats
    int dayOfWeek{0};          // 0 = Sunday .. 6 = Saturday
    int hour{0};
    int minute{0};
};

/// Everything the sequencer needs to know about "now".
struct CueTick {
    double nowSeconds{0.0};  // monotonic; AfterPrevious in seconds and post-wait use it
    ClockState clock;        // latest known transport state; AfterPrevious in beats uses clock.beat
    WallClock wall;
};

class CueSequencer {
public:
    explicit CueSequencer(CueList list = {}, std::uint64_t seed = 1);

    /// Replaces the list and goes back to "nothing fired".
    void setList(CueList list);
    const CueList& list() const noexcept { return list_; }

    /// Back to "nothing fired yet"; the first cue's automatic trigger (if any)
    /// is timed from `tick`.
    void arm(const CueTick& tick);

    /// Index of the cue that fired last, -1 if none yet.
    int current() const noexcept { return current_; }
    /// Index of the cue Go would fire, -1 if the list is empty or finished.
    int next() const noexcept;

    // Each of these returns the index of the cue to recall, if one fired.
    std::optional<int> go(const CueTick& tick);
    std::optional<int> back(const CueTick& tick);
    std::optional<int> jump(int index, const CueTick& tick);
    /// Evaluates AfterPrevious and TimeOfDay of the next cue.
    std::optional<int> update(const CueTick& tick);
    std::optional<int> onOsc(const std::string& address, const CueTick& tick);
    /// channel 1..16 (0 is "any" in a trigger, not a valid incoming channel).
    std::optional<int> onMidi(trigger::Midi::Type type, int number, int channel, const CueTick& tick);

    struct BeatTrigger {
        int cueIndex{0};
        double beat{0.0};  // absolute transport beat, 0 = start
    };
    /// Set when the next cue has an AtBeat trigger.
    std::optional<BeatTrigger> pendingBeatTrigger(int beatsPerBar) const;
    /// The engine fired the AtBeat cue `cueIndex` (must be next()).
    std::optional<int> beatFired(int cueIndex, const CueTick& tick);

    /// Absolute beat of an AtBeat trigger (bar and beat are 1-based).
    static double beatPosition(const trigger::AtBeat& t, int beatsPerBar);

private:
    std::optional<int> start(int index, const CueTick& tick);
    void scheduleNext(const CueTick& tick);

    CueList list_;
    Rng rng_;
    int current_{-1};

    // AfterPrevious state for the cue that is next.
    bool deadlineActive_{false};
    bool deadlineInBeats_{false};
    double deadlineAt_{0.0};
    double postWaitEnd_{0.0};
    long long lastTimeOfDayStamp_{-1};
};

}  // namespace spacer
