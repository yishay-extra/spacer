#include <catch2/catch_test_macros.hpp>

#include "ui/logic/TriggerText.h"

using namespace spacer;
namespace tt = spacer::triggertext;

namespace {
template <class T>
const T* as(const std::optional<Trigger>& t) {
    return t ? std::get_if<T>(&t->value) : nullptr;
}
}  // namespace

TEST_CASE("TriggerText: every trigger kind formats and parses back", "[ui]") {
    const std::vector<Trigger> all = {
        Trigger{trigger::Manual{}},
        Trigger{trigger::AtBeat{5, 2.5}},
        Trigger{trigger::AfterPrevious{Period::seconds(4), 0.0}},
        Trigger{trigger::AfterPrevious{Period::beats(2), 1.5}},
        Trigger{trigger::Osc{"/Cue/Intro"}},
        Trigger{trigger::Midi{trigger::Midi::Type::Note, 60, 0}},
        Trigger{trigger::Midi{trigger::Midi::Type::Cc, 7, 3}},
        Trigger{trigger::TimeOfDay{9, 5, {}}},
        Trigger{trigger::TimeOfDay{22, 30, {1, 3, 5}}},
    };
    for (const auto& t : all) {
        const auto text = tt::format(t);
        const auto parsed = tt::parse(text);
        INFO(text);
        REQUIRE(parsed);
        CHECK(*parsed == t);
        CHECK(tt::format(*parsed) == text);
    }
    for (int k = 0; k <= static_cast<int>(tt::Kind::TimeOfDay); ++k) {
        const auto d = tt::defaultTrigger(static_cast<tt::Kind>(k));
        CHECK(tt::kindOf(d) == static_cast<tt::Kind>(k));
        CHECK(tt::parse(tt::format(d)) == d);
    }
}

TEST_CASE("TriggerText: forgiving input", "[ui]") {
    CHECK(as<trigger::Manual>(tt::parse("  GO ")));
    const auto a = tt::parse("After 4s");
    REQUIRE(as<trigger::AfterPrevious>(a));
    CHECK(as<trigger::AfterPrevious>(a)->delay == Period::seconds(4));
    const auto b = tt::parse("after 2 beats +1");
    REQUIRE(as<trigger::AfterPrevious>(b));
    CHECK(as<trigger::AfterPrevious>(b)->delay == Period::beats(2));
    CHECK(as<trigger::AfterPrevious>(b)->randomRange == 1.0);
    CHECK(as<trigger::AfterPrevious>(tt::parse("after 3"))->delay == Period::seconds(3));
    CHECK(as<trigger::AtBeat>(tt::parse("bar 3"))->beat == 1.0);
    CHECK(as<trigger::Midi>(tt::parse("CC 7 channel 2"))->channel == 2);
    CHECK(as<trigger::Midi>(tt::parse("note 60 ch 16"))->channel == 16);
    // OSC addresses keep their case.
    CHECK(as<trigger::Osc>(tt::parse("OSC /Go/Now"))->address == "/Go/Now");
}

TEST_CASE("TriggerText: time of day days", "[ui]") {
    const auto weekdays = tt::parse("22:00 mon-fri");
    REQUIRE(as<trigger::TimeOfDay>(weekdays));
    CHECK(as<trigger::TimeOfDay>(weekdays)->days == std::vector<int>{1, 2, 3, 4, 5});
    CHECK(tt::format(*weekdays) == "22:00 Mon,Tue,Wed,Thu,Fri");
    CHECK(as<trigger::TimeOfDay>(tt::parse("08:15 fri-mon"))->days == std::vector<int>{0, 1, 5, 6});
    CHECK(as<trigger::TimeOfDay>(tt::parse("08:15 sun mon tue wed thu fri sat"))->days.empty());
    CHECK(as<trigger::TimeOfDay>(tt::parse("08:15 daily"))->days.empty());
    CHECK(as<trigger::TimeOfDay>(tt::parse("8:05 SAT,sun"))->days == std::vector<int>{0, 6});
}

TEST_CASE("TriggerText: invalid input is rejected", "[ui]") {
    for (const char* bad : {"",
                            "   ",
                            "banana",
                            "manual now",
                            "after",
                            "after x",
                            "after -1",
                            "after 4 parsecs",
                            "after 4 s 5",
                            "after 4 s +x",
                            "after 99999 s",
                            "bar 0",
                            "bar 1 beat 0.5",
                            "bar x",
                            "bar 1 beat",
                            "osc",
                            "osc cue",
                            "osc /",
                            "osc /a /b",
                            "note",
                            "note 128",
                            "note 60 ch0",
                            "note 60 ch17",
                            "note 60 foo",
                            "cc -1",
                            "24:00",
                            "12:60",
                            "12:00 funday",
                            "12:00 mon-xyz",
                            "1:2:3",
                            ":30"})
        CHECK_FALSE(tt::parse(bad));
}
