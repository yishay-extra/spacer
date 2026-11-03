#include <catch2/catch_test_macros.hpp>
#include <set>

#include "core/CueSequencer.h"

using namespace spacer;

namespace {

Cue cue(std::string name, Trigger t = {}, float postWait = 0.0f) {
    Cue c;
    c.name = name;
    c.sceneName = name;
    c.trigger = std::move(t);
    c.postWaitSeconds = postWait;
    return c;
}

Trigger after(double seconds, double range = 0.0) {
    return {trigger::AfterPrevious{Period::seconds(seconds), range}};
}

CueTick at(double seconds) {
    CueTick t;
    t.nowSeconds = seconds;
    return t;
}

CueList listOf(std::vector<Cue> cues, bool loop = false) {
    CueList l;
    l.name = "L";
    l.cues = std::move(cues);
    l.loop = loop;
    return l;
}

}  // namespace

TEST_CASE("CueSequencer: Go walks the list and stops at the end") {
    CueSequencer s(listOf({cue("a"), cue("b"), cue("c")}));
    CHECK(s.current() == -1);
    CHECK(s.next() == 0);
    CHECK(s.go(at(0)) == 0);
    CHECK(s.go(at(1)) == 1);
    CHECK(s.go(at(2)) == 2);
    CHECK(s.next() == -1);
    CHECK_FALSE(s.go(at(3)).has_value());
    CHECK(s.current() == 2);
}

TEST_CASE("CueSequencer: a looping list wraps from the last cue to the first") {
    CueSequencer s(listOf({cue("a"), cue("b")}, true));
    CHECK(s.go(at(0)) == 0);
    CHECK(s.go(at(0)) == 1);
    CHECK(s.next() == 0);
    CHECK(s.go(at(0)) == 0);
    CHECK(s.go(at(0)) == 1);
    CHECK(s.go(at(0)) == 0);
}

TEST_CASE("CueSequencer: Back and Jump") {
    CueSequencer s(listOf({cue("a"), cue("b"), cue("c")}));
    CHECK_FALSE(s.back(at(0)).has_value());  // nothing fired yet
    CHECK(s.jump(2, at(0)) == 2);
    CHECK(s.back(at(0)) == 1);
    CHECK(s.back(at(0)) == 0);
    CHECK_FALSE(s.back(at(0)).has_value());  // first cue, no loop
    CHECK(s.go(at(0)) == 1);                 // Go continues after the cue Back landed on
    CHECK_FALSE(s.jump(7, at(0)).has_value());
    CHECK_FALSE(s.jump(-1, at(0)).has_value());
    CHECK(s.current() == 1);
}

TEST_CASE("CueSequencer: Back from the first cue wraps when looping") {
    CueSequencer s(listOf({cue("a"), cue("b"), cue("c")}, true));
    s.go(at(0));
    CHECK(s.back(at(0)) == 2);
}

TEST_CASE("CueSequencer: empty list never fires") {
    CueSequencer s;
    CHECK(s.next() == -1);
    CHECK_FALSE(s.go(at(0)).has_value());
    CHECK_FALSE(s.update(at(100)).has_value());
}

TEST_CASE("CueSequencer: AfterPrevious fires once its delay has elapsed since the previous cue started") {
    CueSequencer s(listOf({cue("a"), cue("b", after(5.0)), cue("c", after(2.0))}));
    REQUIRE(s.go(at(10)) == 0);
    CHECK_FALSE(s.update(at(14.9)).has_value());
    CHECK(s.update(at(15.0)) == 1);
    CHECK_FALSE(s.update(at(16.9)).has_value());
    CHECK(s.update(at(17.0)) == 2);
    CHECK_FALSE(s.update(at(100)).has_value());  // end of list
}

TEST_CASE("CueSequencer: arm() times the first cue's AfterPrevious trigger") {
    CueSequencer s(listOf({cue("a", after(3.0))}));
    s.arm(at(100));
    CHECK_FALSE(s.update(at(102.9)).has_value());
    CHECK(s.update(at(103.0)) == 0);
}

TEST_CASE("CueSequencer: AfterPrevious in beats follows the transport") {
    Trigger t{trigger::AfterPrevious{Period::beats(4), 0.0}};
    CueSequencer s(listOf({cue("a"), cue("b", t)}));
    CueTick tick = at(0);
    tick.clock.playing = true;
    tick.clock.beat = 8.0;
    REQUIRE(s.go(tick) == 0);
    tick.clock.beat = 11.9;
    CHECK_FALSE(s.update(tick).has_value());
    tick.clock.beat = 12.0;
    CHECK(s.update(tick) == 1);
}

TEST_CASE("CueSequencer: AfterPrevious in beats does not elapse while the transport is stopped") {
    Trigger t{trigger::AfterPrevious{Period::beats(1), 0.0}};
    CueSequencer s(listOf({cue("a"), cue("b", t)}));
    CueTick tick = at(0);
    tick.clock.playing = false;
    tick.clock.beat = 0.0;
    s.go(tick);
    tick.clock.beat = 5.0;
    CHECK_FALSE(s.update(tick).has_value());
    tick.clock.playing = true;
    CHECK(s.update(tick) == 1);
}

TEST_CASE("CueSequencer: post-wait delays an AfterPrevious successor") {
    CueSequencer s(listOf({cue("a", {}, 10.0f), cue("b", after(2.0))}));
    s.go(at(0));
    CHECK_FALSE(s.update(at(5)).has_value());  // own delay elapsed, post-wait not
    CHECK(s.update(at(10)) == 1);
}

TEST_CASE("CueSequencer: AfterPrevious with a random range stays within the range over 1000 runs") {
    const double lo = 4.0, range = 3.0;
    double minSeen = 1e9, maxSeen = 0.0;
    std::set<long long> distinct;
    for (int run = 0; run < 1000; ++run) {
        CueSequencer s(listOf({cue("a"), cue("b", after(lo, range))}), /*seed*/ 1000 + run);
        REQUIRE(s.go(at(50)) == 0);
        // Find the firing time by scanning in 1 ms steps.
        double fired = -1.0;
        for (int ms = 0; ms <= 8000; ++ms) {
            const double now = 50.0 + ms * 0.001;
            if (s.update(at(now))) {
                fired = now - 50.0;
                break;
            }
        }
        REQUIRE(fired >= 0.0);
        minSeen = std::min(minSeen, fired);
        maxSeen = std::max(maxSeen, fired);
        CHECK(fired >= lo - 1e-9);
        CHECK(fired <= lo + range + 0.0011);
        distinct.insert(static_cast<long long>(fired * 1000.0));
    }
    // And it is actually random: both ends of the range are approached.
    CHECK(minSeen < lo + 0.2 * range);
    CHECK(maxSeen > lo + 0.8 * range);
    CHECK(distinct.size() > 100);
}

TEST_CASE("CueSequencer: same seed gives the same random delays") {
    auto firing = [](std::uint64_t seed) {
        CueSequencer s(listOf({cue("a"), cue("b", after(1.0, 5.0))}), seed);
        s.go(at(0));
        for (int ms = 0; ms < 7000; ++ms)
            if (s.update(at(ms * 0.001))) return ms;
        return -1;
    };
    CHECK(firing(7) == firing(7));
    CHECK(firing(7) != firing(8));
}

TEST_CASE("CueSequencer: a looping list advances forever on AfterPrevious") {
    CueSequencer s(listOf({cue("a", after(1.0)), cue("b", after(1.0))}, true));
    s.arm(at(0));
    int fired = 0;
    int last = -1;
    for (int ms = 0; ms <= 10000; ++ms)
        if (auto i = s.update(at(ms * 0.001))) {
            CHECK(*i == (last + 1) % 2);
            last = *i;
            ++fired;
        }
    CHECK(fired >= 9);
}

TEST_CASE("CueSequencer: TimeOfDay fires once in its minute on the right days") {
    trigger::TimeOfDay tod;
    tod.hour = 8;
    tod.minute = 30;
    tod.days = {1, 2, 3, 4, 5};
    CueSequencer s(listOf({cue("morning", {tod}), cue("next")}));

    CueTick t = at(0);
    t.wall = {1000, 1, 8, 29};
    CHECK_FALSE(s.update(t).has_value());
    t.wall = {1001, 0, 8, 30};  // Sunday: excluded
    CHECK_FALSE(s.update(t).has_value());
    t.wall = {1441, 1, 8, 30};  // Monday
    CHECK(s.update(t) == 0);
    t.wall = {1441, 1, 8, 30};
    CHECK_FALSE(s.update(t).has_value());  // next cue is Manual
}

TEST_CASE("CueSequencer: TimeOfDay does not fire twice in the same minute even when the list loops") {
    trigger::TimeOfDay tod{12, 0, {}};
    CueSequencer s(listOf({cue("noon", {tod})}, true));
    CueTick t = at(0);
    t.wall = {500, 3, 12, 0};
    CHECK(s.update(t) == 0);
    t.nowSeconds = 1.0;
    CHECK_FALSE(s.update(t).has_value());
    t.wall = {1940, 3, 12, 0};  // next day, same minute of day
    CHECK(s.update(t) == 0);
}

TEST_CASE("CueSequencer: Osc and Midi triggers apply to the next cue only") {
    trigger::Midi note{trigger::Midi::Type::Note, 60, 0};
    trigger::Midi cc{trigger::Midi::Type::Cc, 7, 2};
    CueSequencer s(listOf({cue("a", {trigger::Osc{"/go/a"}}), cue("b", {note}), cue("c", {cc})}));

    CHECK_FALSE(s.onOsc("/go/b", at(0)).has_value());
    CHECK_FALSE(s.onMidi(trigger::Midi::Type::Note, 60, 1, at(0)).has_value());  // b is not next yet
    CHECK(s.onOsc("/go/a", at(0)) == 0);
    CHECK_FALSE(s.onOsc("/go/a", at(0)).has_value());
    CHECK_FALSE(s.onMidi(trigger::Midi::Type::Note, 61, 1, at(0)).has_value());
    CHECK_FALSE(s.onMidi(trigger::Midi::Type::Cc, 60, 1, at(0)).has_value());
    CHECK(s.onMidi(trigger::Midi::Type::Note, 60, 9, at(0)) == 1);            // channel 0 = any
    CHECK_FALSE(s.onMidi(trigger::Midi::Type::Cc, 7, 3, at(0)).has_value());  // wrong channel
    CHECK(s.onMidi(trigger::Midi::Type::Cc, 7, 2, at(0)) == 2);
}

TEST_CASE("CueSequencer: AtBeat is exposed as a pending beat trigger for the engine") {
    CueSequencer s(listOf({cue("a"), cue("b", {trigger::AtBeat{3, 2.0}}), cue("c")}));
    CHECK_FALSE(s.pendingBeatTrigger(4).has_value());  // a is Manual
    s.go(at(0));
    const auto p = s.pendingBeatTrigger(4);
    REQUIRE(p.has_value());
    CHECK(p->cueIndex == 1);
    CHECK(p->beat == 9.0);  // bar 3 beat 2 = (3-1)*4 + 1
    CHECK(s.pendingBeatTrigger(3)->beat == 7.0);
    CHECK_FALSE(s.update(at(100)).has_value());      // update() never fires AtBeat
    CHECK_FALSE(s.beatFired(2, at(0)).has_value());  // not the next cue
    CHECK(s.beatFired(1, at(0)) == 1);
    CHECK(s.current() == 1);
    CHECK_FALSE(s.pendingBeatTrigger(4).has_value());
}

TEST_CASE("CueSequencer: bar 1 beat 1 is beat 0") {
    CHECK(CueSequencer::beatPosition({1, 1.0}, 4) == 0.0);
    CHECK(CueSequencer::beatPosition({2, 1.0}, 3) == 3.0);
}

TEST_CASE("CueSequencer: setList resets the position") {
    CueSequencer s(listOf({cue("a"), cue("b")}));
    s.go(at(0));
    s.setList(listOf({cue("x")}));
    CHECK(s.current() == -1);
    CHECK(s.next() == 0);
}

TEST_CASE("CueSequencer: updateList keeps the position and re-times only a changed next cue") {
    CueSequencer s(listOf({cue("a"), cue("b", after(10)), cue("c")}));
    s.arm(at(0));
    REQUIRE(s.go(at(0)) == 0);
    CHECK(s.current() == 0);

    // Editing a cue that is not next leaves the countdown (due at t = 10) alone.
    auto edited = s.list();
    edited.cues[2].name = "cc";
    s.updateList(edited, at(5));
    CHECK(s.current() == 0);
    CHECK_FALSE(s.update(at(9.9)));
    CHECK(s.update(at(10.0)) == 1);

    // Changing the next cue's delay re-times it from "now".
    s.arm(at(20));
    REQUIRE(s.go(at(20)) == 0);
    edited = s.list();
    edited.cues[1].trigger = after(3);
    s.updateList(edited, at(21));
    CHECK_FALSE(s.update(at(23.9)));
    CHECK(s.update(at(24.0)) == 1);

    // Inserting a cue shifts "next"; removing the current cue clamps.
    edited = s.list();
    edited.cues.erase(edited.cues.begin() + 1, edited.cues.end());
    s.updateList(edited, at(30));
    CHECK(s.current() == 0);
    CHECK(s.next() == -1);
    s.updateList(listOf({}), at(30));
    CHECK(s.current() == -1);
    CHECK(s.next() == -1);
}
