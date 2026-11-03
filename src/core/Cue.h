#pragma once

// Cues and cue lists (SPEC §11).

#include <string>
#include <variant>
#include <vector>

#include "core/ExactEqual.h"
#include "core/MotionSpec.h"

namespace spacer {

namespace trigger {
struct Manual {
    bool operator==(const Manual&) const = default;
};
/// bar and beat are 1-based (bar 1 beat 1 = transport start).
struct AtBeat {
    int bar{1};
    double beat{1.0};
    bool operator==(const AtBeat& o) const { return bar == o.bar && exactlyEqual(beat, o.beat); }
};
/// Fires `delay` after the previous cue starts; a random extra in
/// [0, randomRange] (same unit) is added for installation variation.
struct AfterPrevious {
    Period delay{Period::seconds(0)};
    double randomRange{0.0};
    bool operator==(const AfterPrevious& o) const {
        return delay == o.delay && exactlyEqual(randomRange, o.randomRange);
    }
};
struct Osc {
    std::string address;
    bool operator==(const Osc&) const = default;
};
struct Midi {
    enum class Type { Note, Cc };
    Type type{Type::Note};
    int number{0};
    int channel{0};  // 0 = any, 1..16
    bool operator==(const Midi&) const = default;
};
struct TimeOfDay {
    int hour{0};
    int minute{0};
    std::vector<int> days;  // 0 = Sunday .. 6 = Saturday; empty = every day
    bool operator==(const TimeOfDay&) const = default;
};
}  // namespace trigger

struct Trigger {
    std::variant<trigger::Manual, trigger::AtBeat, trigger::AfterPrevious, trigger::Osc, trigger::Midi,
                 trigger::TimeOfDay>
        value{trigger::Manual{}};
    bool operator==(const Trigger&) const = default;
};

struct Cue {
    std::string name;
    std::string sceneName;  // JSON key "scene"
    float transitionSeconds{0.0f};
    Trigger trigger;
    bool restartMotionPhase{false};
    float postWaitSeconds{0.0f};
    bool operator==(const Cue& o) const {
        return name == o.name && sceneName == o.sceneName &&
               exactlyEqual(transitionSeconds, o.transitionSeconds) && trigger == o.trigger &&
               restartMotionPhase == o.restartMotionPhase && exactlyEqual(postWaitSeconds, o.postWaitSeconds);
    }
};

struct CueList {
    std::string name;
    std::vector<Cue> cues;
    bool loop{false};
    bool operator==(const CueList&) const = default;
};

}  // namespace spacer
