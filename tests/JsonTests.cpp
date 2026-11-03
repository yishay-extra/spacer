#include <catch2/catch_test_macros.hpp>

#include "TestModels.h"
#include "core/Json.h"
#include "core/Validate.h"

using namespace spacer;
using namespace spacer::testing;

#define ROUND_TRIP(...)                           \
    do {                                          \
        const auto original = (__VA_ARGS__);      \
        REQUIRE(roundTrip(original) == original); \
    } while (0)

TEST_CASE("Geometry types round-trip and use the §15 shapes") {
    ROUND_TRIP(Cartesian(1.5f, -2.0f, 0.25f));
    ROUND_TRIP(Position(-135.0f, 33.3f, 4.2f));
    const Json p = Position{10.0f, 20.0f, 3.0f};
    REQUIRE(p == Json::parse(R"({"az":10,"el":20,"dist":3})"));
    ROUND_TRIP(Speaker{3, "Rs", {110.0f, 0.0f, 2.0f}, -1.5f, 3.0f, true, 5});
    ROUND_TRIP(Speaker{4, "LFE", {0.0f, 0.0f, 2.0f}, 0.0f, 0.0f, false, 3, true});
    ROUND_TRIP(Layout{
        "Mine", {Speaker{1, "a", {0, 0, 1}, 0, 0, false, -1}, Speaker{2, "b", {90, 0, 1}, 0, 0, false, 0}}});
}

TEST_CASE("Period serialises as {beats} or {seconds}") {
    REQUIRE(Json(Period::beats(16)) == Json::parse(R"({"beats":16})"));
    REQUIRE(Json(Period::seconds(2.5)) == Json::parse(R"({"seconds":2.5})"));
    ROUND_TRIP(Period::beats(0.125));
    ROUND_TRIP(Period::seconds(12.0));
    REQUIRE_THROWS(Json::parse(R"({"minutes":1})").get<Period>());
}

TEST_CASE("Floats are written as short decimals") {
    const Json j = Position{0.2f, 0.1f, 1.1f};
    REQUIRE(j.dump() == R"({"az":0.2,"dist":1.1,"el":0.1})");
}

TEST_CASE("Every motion kind round-trips") {
    for (const auto& m : allMotions()) {
        ROUND_TRIP(m);
    }
    for (const auto& m : allMotions()) {
        const Json j = m;
        REQUIRE(j.contains("kind"));
    }
}

TEST_CASE("Motion JSON discriminator and unknown kinds") {
    const Json j = MotionSpec{OrbitMotion{}};
    REQUIRE(j["kind"] == "Orbit");
    REQUIRE_THROWS(Json::parse(R"({"kind":"Wobble"})").get<MotionSpec>());
    REQUIRE_THROWS(Json::parse(R"({"radius":1})").get<MotionSpec>());
}

TEST_CASE("Modulators, plugin slots, distance model round-trip") {
    for (const auto& m : allModulators()) ROUND_TRIP(m);
    ROUND_TRIP(makeSlot());
    ROUND_TRIP(DistanceModel{2.0f, 0.25f, 1.5f});
}

TEST_CASE("Every source kind round-trips") {
    for (const auto& s : allSources()) ROUND_TRIP(s);
}

TEST_CASE("Source input shapes follow §15") {
    SourceSpec s;
    s.id = 1;
    s.input.value = VirtualInputSpec{{0, 1}};
    Json j = s;
    REQUIRE(j["kind"] == "VirtualInput");
    REQUIRE(j["input"] == Json::parse(R"({"left":0,"right":1})"));
    s.input.value = VirtualInputSpec{{5, -1}};
    j = s;
    REQUIRE(j["input"] == Json::parse(R"({"channel":5})"));
    s.input.value = GeneratorSpec{GeneratorKind::Impulse, 0.0f, 2.0f};
    j = s;
    REQUIRE(j["input"] == Json::parse(R"({"type":"impulse","periodSeconds":2})"));
}

TEST_CASE("Speaker chains, master, scenes round-trip") {
    ROUND_TRIP(Eq3{});
    ROUND_TRIP(makeChain(4));
    ROUND_TRIP(MasterSpec{-12.0f});
    ROUND_TRIP(makeScene("S"));
}

TEST_CASE("Every trigger, cue and cue list round-trips") {
    for (const auto& t : allTriggers()) ROUND_TRIP(t);
    Cue c{"Go", "Intro", 3.0f, Trigger{trigger::AtBeat{2, 1.0}}, true, 4.0f};
    ROUND_TRIP(c);
    const Json j = c;
    REQUIRE(j["scene"] == "Intro");
    ROUND_TRIP(CueList{"L", {c, c}, true});
}

TEST_CASE("TimeOfDay uses HH:MM") {
    const Json j = Trigger{trigger::TimeOfDay{7, 5, {0, 6}}};
    REQUIRE(j["time"] == "07:05");
    REQUIRE_THROWS(Json::parse(R"({"kind":"TimeOfDay","time":"25:00"})").get<Trigger>());
    REQUIRE_THROWS(Json::parse(R"({"kind":"TimeOfDay","time":"noon"})").get<Trigger>());
}

TEST_CASE("Project configuration types round-trip") {
    ROUND_TRIP((DeviceMapping{"Dev", 44100.0, 512, 331.0f}));
    ROUND_TRIP(AmbisonicsConfig{4});
    ROUND_TRIP((OscConfig{9005, {"example.local", 9006, true}}));
    ROUND_TRIP((MidiMapping{MidiMessageType::Nrpn, 2, 100, "gain", 0.0f, 1.0f, MidiCurve::Log}));
    ROUND_TRIP((MidiConfig{{MidiMapping{MidiMessageType::Cc, 1, 7, "gain"}}, 16}));
    ROUND_TRIP((ClockConfig{ClockKind::Osc, 99.0, 7, true}));
}

TEST_CASE("MidiConfig::programChangeChannel defaults to off and is optional in JSON") {
    REQUIRE(MidiConfig{}.programChangeChannel == 0);
    const auto cfg = Json::parse(R"({"mappings":[]})").get<MidiConfig>();
    REQUIRE(cfg.programChangeChannel == 0);
}

TEST_CASE("Full project round-trips") {
    ROUND_TRIP(makeFullProject());
    ROUND_TRIP(Project{});
}

TEST_CASE("Wrong types and unknown enums throw, not crash") {
    auto bad = [](const char* text) { return Json::parse(text).get<Project>(); };
    REQUIRE_THROWS(bad(R"([])"));
    REQUIRE_THROWS(bad(R"({"layout": 3})"));
    REQUIRE_THROWS(bad(R"({"layout": {"speakers": [{"name":"x"}]}})"));  // speaker without id
    REQUIRE_THROWS(bad(R"({"sources": [{"id": 1, "kind": "Telepathy"}]})"));
    REQUIRE_THROWS(bad(R"({"sources": [{"kind": "File"}]})"));  // no id
    REQUIRE_THROWS(bad(R"({"sources": [{"id": "one", "kind": "File"}]})"));
    REQUIRE_THROWS(bad(R"({"sources": [{"id": 1, "kind": "File", "spatializer": "Magic"}]})"));
    REQUIRE_THROWS(bad(R"({"clock": {"bpm": "fast"}})"));
    REQUIRE_THROWS(bad(R"({"sources": "none"})"));
}

TEST_CASE("Error messages carry the key path") {
    try {
        Json::parse(R"({"sources":[{"id":1,"kind":"File","motion":{"kind":"Orbit","radius":"big"}}]})")
            .get<Project>();
        FAIL("expected an exception");
    } catch (const std::exception& e) {
        const std::string msg = e.what();
        REQUIRE(msg.find("sources[0]") != std::string::npos);
        REQUIRE(msg.find("radius") != std::string::npos);
    }
}

TEST_CASE("Speaker lfe key is optional and defaults to false") {
    const auto sp = Json::parse(R"({"id":1,"name":"L","position":{"az":0,"el":0,"dist":2}})").get<Speaker>();
    REQUIRE_FALSE(sp.lfe);
}

TEST_CASE("ClockConfig::latencyTrimMs round-trips, is optional and validated (WP14)") {
    ROUND_TRIP((ClockConfig{ClockKind::Midi, 120.0, 4, false, -12.5f}));
    ClockConfig c =
        Json::parse(R"({"kind":"Midi","bpm":120,"beatsPerBar":4,"quantisedStart":false})").get<ClockConfig>();
    CHECK(c.latencyTrimMs == 0.0f);
    Project p;
    p.clock.latencyTrimMs = 5000.0f;
    bool flagged = false;
    for (const auto& issue : validate(p)) flagged = flagged || issue.path == "clock.latencyTrimMs";
    CHECK(flagged);
}
