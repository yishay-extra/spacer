#include <catch2/catch_test_macros.hpp>

#include "ui/logic/AppSettings.h"
#include "ui/logic/ProjectHistory.h"
#include "ui/logic/SettingsModel.h"

using namespace spacer;
using namespace spacer::settings;

TEST_CASE("Settings: sample rate and buffer choices follow the device", "[ui][settings]") {
    CHECK(sampleRateChoices({}, 48000.0) == std::vector<double>{44100.0, 48000.0, 88200.0, 96000.0});
    CHECK(sampleRateChoices({44100.0, 48000.0, 192000.0}, 48000.0) == std::vector<double>{44100.0, 48000.0});
    // The project's rate stays selectable even if the device does not offer it.
    CHECK(sampleRateChoices({48000.0}, 96000.0) == std::vector<double>{48000.0, 96000.0});
    CHECK(bufferSizeChoices({}, 128) == std::vector<int>{32, 64, 128, 256, 512, 1024, 2048});
    CHECK(bufferSizeChoices({16, 64, 100, 4096}, 64) == std::vector<int>{64, 100});
    CHECK(bufferSizeChoices({64}, 300) == std::vector<int>{64, 300});
    CHECK(formatSampleRate(44100.0) == "44.1 kHz");
    CHECK(formatSampleRate(48000.0) == "48 kHz");
    CHECK(formatBufferSize(128, 48000.0) == "128 (2.7 ms)");
    CHECK(formatBufferSize(128, 0.0) == "128");
}

TEST_CASE("Settings: text fields are parsed and range-checked", "[ui][settings]") {
    CHECK(*parsePort(" 9000 ").value == 9000);
    CHECK_FALSE(parsePort("0"));
    CHECK_FALSE(parsePort("65536"));
    CHECK_FALSE(parsePort("90a0"));
    CHECK_FALSE(parsePort("").error.empty());
    CHECK(*parseHost(" 192.168.1.5 ").value == "192.168.1.5");
    CHECK_FALSE(parseHost("   "));
    CHECK_FALSE(parseHost("my host"));
    CHECK(*parseBpm("128.5").value == 128.5);
    CHECK_FALSE(parseBpm("19.9"));
    CHECK_FALSE(parseBpm("301"));
    CHECK_FALSE(parseBpm("nan"));
    CHECK(*parseBeatsPerBar("7").value == 7);
    CHECK_FALSE(parseBeatsPerBar("0"));
    CHECK_FALSE(parseBeatsPerBar("2.5"));
    CHECK(*parseSpeedOfSound("343").value == 343.0f);
    CHECK_FALSE(parseSpeedOfSound("299"));
    CHECK_FALSE(parseSpeedOfSound("inf"));
}

TEST_CASE("Settings: classify says what a change needs", "[ui][settings]") {
    const SettingsFields base = fieldsOf(Project{});
    CHECK_FALSE(classify(base, base).any());

    auto f = base;
    f.devices.sampleRate = 96000.0;
    CHECK(classify(base, f).device);
    CHECK_FALSE(classify(base, f).graph);

    f = base;
    f.devices.outputDeviceName = "Interface";
    CHECK(classify(base, f).device);
    f = base;
    f.devices.bufferSize = 256;
    CHECK(classify(base, f).device);

    f = base;
    f.devices.speedOfSound = 340.0f;
    CHECK(classify(base, f).graph);
    CHECK_FALSE(classify(base, f).device);

    f = base;
    f.clock.kind = ClockKind::Midi;
    CHECK(classify(base, f).graph);
    f = base;
    f.clock.bpm = 90.0;
    CHECK(classify(base, f).graph);

    f = base;
    f.osc.inPort = 9100;
    CHECK(classify(base, f).osc);
    CHECK_FALSE(classify(base, f).graph);
    f = base;
    f.osc.out.enabled = true;
    CHECK(classify(base, f).osc);

    f = base;
    f.programChangeChannel = 3;
    CHECK(classify(base, f).midi);
    CHECK_FALSE(classify(base, f).osc);
}

TEST_CASE("Settings: validate rejects what the engine or the spec cannot take", "[ui][settings]") {
    const SettingsFields ok = fieldsOf(Project{});
    CHECK(validate(ok).empty());
    auto f = ok;
    f.devices.bufferSize = 16;
    CHECK_FALSE(validate(f).empty());
    f = ok;
    f.devices.bufferSize = 4096;
    CHECK_FALSE(validate(f).empty());
    f = ok;
    f.devices.sampleRate = 0.0;
    CHECK_FALSE(validate(f).empty());
    f = ok;
    f.devices.speedOfSound = 0.0f;
    CHECK_FALSE(validate(f).empty());
    f = ok;
    f.osc.inPort = 70000;
    CHECK_FALSE(validate(f).empty());
    f = ok;
    f.osc.out.host.clear();
    CHECK_FALSE(validate(f).empty());
    f = ok;
    f.programChangeChannel = 17;
    CHECK_FALSE(validate(f).empty());
    f = ok;
    f.clock.bpm = 10.0;
    CHECK_FALSE(validate(f).empty());
    f = ok;
    f.clock.beatsPerBar = 0;
    CHECK_FALSE(validate(f).empty());
}

TEST_CASE("Settings: the edit applies, undoes and picks the engine effect", "[ui][settings]") {
    Project project;
    ProjectHistory history;

    auto after = fieldsOf(project);
    after.clock.kind = ClockKind::Mtc;
    after.osc.inPort = 9100;
    auto edit = edits::setSettings(project, after, "Clock Source");
    CHECK(edit.effect.kind == EngineEffect::Kind::Structural);
    const auto effect = history.perform(project, std::move(edit));
    REQUIRE(effect);
    CHECK(project.clock.kind == ClockKind::Mtc);
    CHECK(project.osc.inPort == 9100);

    history.undo(project);
    CHECK(project.clock.kind == ClockKind::Internal);
    CHECK(project.osc.inPort == 9000);
    history.redo(project);
    CHECK(project.clock.kind == ClockKind::Mtc);

    // Device-only and OSC-only edits need no graph rebuild by themselves.
    auto device = fieldsOf(project);
    device.devices.bufferSize = 512;
    CHECK(edits::setSettings(project, device).effect.kind == EngineEffect::Kind::None);

    // Only the settings fields are touched.
    Project other = project;
    other.name = "Other";
    other.sources.emplace_back();
    auto e = edits::setSettings(project, device);
    e.apply(other);
    CHECK(other.name == "Other");
    CHECK(other.sources.size() == 1);
    CHECK(other.devices.bufferSize == 512);
}

TEST_CASE("Settings: dragging a tempo field is one undo step", "[ui][settings]") {
    Project project;
    ProjectHistory history;
    history.beginGesture();
    for (const double bpm : {100.0, 110.0, 120.5}) {
        auto f = fieldsOf(project);
        f.clock.bpm = bpm;
        history.perform(project, edits::setSettings(project, f, "Tempo", "tempo"));
    }
    history.endGesture();
    CHECK(project.clock.bpm == 120.5);
    history.undo(project);
    CHECK(project.clock.bpm == 120.0);
    CHECK_FALSE(history.canUndo());
}

TEST_CASE("Settings: clock options list Link only when built", "[ui][settings]") {
    const auto without = clockOptions(false);
    REQUIRE(without.size() == 4);
    CHECK(without[0].kind == ClockKind::Internal);
    for (const auto& o : without) CHECK(o.kind != ClockKind::Link);
    const auto with = clockOptions(true);
    REQUIRE(with.size() == 5);
    CHECK(with[1].kind == ClockKind::Link);
    CHECK(with[1].label == "Ableton Link");
    CHECK(clockKindName(ClockKind::Mtc) == "MTC");
    CHECK_FALSE(clockKindHint(ClockKind::Osc).empty());
}

TEST_CASE("Settings: clock lock text", "[ui][settings]") {
    CHECK(clockLockText(ClockKind::Internal, ClockKind::Internal, false) == "Internal");
    CHECK(clockLockText(ClockKind::Midi, ClockKind::Midi, true) == "Locked");
    CHECK(clockLockText(ClockKind::Midi, ClockKind::Midi, false) == "Searching...");
    CHECK(clockLockText(ClockKind::Link, ClockKind::Internal, false) == "Unavailable, running internal");
}

TEST_CASE("Settings: aggregate request", "[ui][settings]") {
    const std::vector<AudioDeviceInfo> devices{{"uid-a", "Interface A", 2, 8},
                                               {"uid-b", "Interface B", 2, 8},
                                               {"uid-mic", "Microphone", 1, 0},
                                               {"uid-spacer", "Spacer", 0, 64}};
    CHECK(aggregateCandidates(devices).size() == 2);
    CHECK(aggregateUid("My Rig 2") == "org.spacer.aggregate.My-Rig-2");

    auto r = planAggregate("My Rig", {"uid-a", "uid-b", "uid-a"}, devices);
    CHECK(r.error.empty());
    CHECK(r.subDeviceUids == std::vector<std::string>{"uid-a", "uid-b"});
    CHECK(r.uid == "org.spacer.aggregate.My-Rig");

    CHECK_FALSE(planAggregate("", {"uid-a", "uid-b"}, devices).error.empty());
    CHECK_FALSE(planAggregate("Solo", {"uid-a"}, devices).error.empty());
    CHECK_FALSE(planAggregate("Dup", {"uid-a", "uid-a"}, devices).error.empty());
    CHECK_FALSE(planAggregate("Interface A", {"uid-a", "uid-b"}, devices).error.empty());  // name taken
    CHECK_FALSE(planAggregate("X", {"uid-a", "uid-mic"}, devices).error.empty());          // no outputs
    CHECK_FALSE(planAggregate("X", {"uid-a", "uid-spacer"}, devices).error.empty());       // virtual device
}

TEST_CASE("AppSettings: JSON round trip, defaults and normalisation", "[ui][settings]") {
    AppSettings s;
    CHECK(s.virtualDeviceLatencyFrames == 512);
    s.virtualDeviceLatencyFrames = 128;
    s = appsettings::withMidiInput(s, "Launchpad", false);
    s = appsettings::withMidiInput(s, "APC40", false);
    CHECK_FALSE(s.midiInputEnabled("APC40"));
    CHECK(s.midiInputEnabled("Other"));
    REQUIRE(s.disabledMidiInputs == std::vector<std::string>{"APC40", "Launchpad"});

    const auto back = appsettings::parse(appsettings::toJsonText(s));
    REQUIRE(back);
    CHECK(*back == s);

    s = appsettings::withMidiInput(s, "APC40", true);
    CHECK(s.disabledMidiInputs == std::vector<std::string>{"Launchpad"});

    CHECK(appsettings::parse("{}")->virtualDeviceLatencyFrames == 512);
    CHECK(appsettings::parse(R"({"virtualDeviceLatencyFrames": 200})")->virtualDeviceLatencyFrames == 256);
    CHECK(appsettings::parse(R"({"virtualDeviceLatencyFrames": 5000})")->virtualDeviceLatencyFrames == 512);
    CHECK(appsettings::parse(R"({"virtualDeviceLatencyFrames": 1})")->virtualDeviceLatencyFrames == 128);
    CHECK(appsettings::parse(R"({"disabledMidiInputs": ["b","a","b"]})")->disabledMidiInputs ==
          std::vector<std::string>{"a", "b"});
    CHECK_FALSE(appsettings::parse("[1]"));
    CHECK_FALSE(appsettings::parse("nonsense"));
    CHECK_FALSE(appsettings::parse(R"({"disabledMidiInputs": 3})"));
}

TEST_CASE("AppSettings: save and load", "[ui][settings]") {
    const auto dir = std::filesystem::temp_directory_path() / "spacer-appsettings-test";
    std::filesystem::remove_all(dir);
    const auto path = dir / "sub" / "settings.json";
    REQUIRE_FALSE(appsettings::load(path));  // missing file
    AppSettings s;
    s.virtualDeviceLatencyFrames = 256;
    s.disabledMidiInputs = {"IAC Driver"};
    REQUIRE_FALSE(appsettings::save(s, path));
    const auto loaded = appsettings::load(path);
    REQUIRE(loaded);
    CHECK(*loaded == s);
    std::filesystem::remove_all(dir);
}

TEST_CASE("Settings: clock latency trim parses, validates and needs no rebuild (WP14)", "[ui][settings]") {
    CHECK(settings::parseLatencyTrim("-12.5").value == -12.5f);
    CHECK_FALSE(settings::parseLatencyTrim("2000"));
    CHECK_FALSE(settings::parseLatencyTrim("abc"));
    Project p;
    auto before = settings::fieldsOf(p);
    auto after = before;
    after.clock.latencyTrimMs = 8.0f;
    CHECK_FALSE(settings::classify(before, after).graph);  // read live, no clock relock
    after.clock.bpm = 100.0;
    CHECK(settings::classify(before, after).graph);
    after.clock.latencyTrimMs = 5000.0f;
    CHECK_FALSE(settings::validate(after).empty());
}
