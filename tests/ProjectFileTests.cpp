#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>

#include "TestModels.h"
#include "core/ProjectFile.h"
#include "core/Validate.h"

using namespace spacer;
using namespace spacer::testing;

namespace {

// The example from SPEC §15, with the "..." placeholders filled in.
const char* kSpecExample = R"json({
  "schemaVersion": 1,
  "name": "Dome show",
  "layout": {
    "name": "Dome 22",
    "speakers": [
      {"id": 1, "name": "1", "position": {"az": 0, "el": 0, "dist": 4.2},
       "trimDb": 0, "userDelayMs": 0, "polarityInverted": false, "outputChannel": 0},
      {"id": 2, "name": "2", "position": {"az": 90, "el": 0, "dist": 4.2},
       "trimDb": 0, "userDelayMs": 0, "polarityInverted": false, "outputChannel": 1}
    ]
  },
  "devices": {"outputDeviceName": "Spacer Aggregate", "sampleRate": 48000, "bufferSize": 128,
              "speedOfSound": 343.0},
  "ambisonics": {"order": 3},
  "sources": [
    {"id": 1, "name": "Synth", "kind": "VirtualInput", "input": {"left": 0, "right": 1},
     "stereo": true, "width": 60, "gainDb": -6, "mute": false,
     "spatializer": "Vbap", "spread": 0.1,
     "distance": {"k": 1.0, "rMin": 0.5, "q": 0},
     "motion": {"kind": "Orbit", "centre": {"az": 0, "el": 20, "dist": 0},
                "radius": 3.0, "period": {"beats": 16}, "direction": 1, "phaseOffset": 0},
     "modulators": [],
     "inserts": []}
  ],
  "scenes": [{"name": "Intro",
              "sources": [
                {"id": 1, "name": "Synth", "kind": "VirtualInput", "input": {"left": 0, "right": 1},
                 "stereo": true, "gainDb": -6, "spatializer": "Vbap"}
              ],
              "speakerChains": [{"speakerId": 1, "trimDb": -1}],
              "master": {"gainDb": 0}}],
  "cueLists": [{"name": "Show", "loop": false,
                "cues": [{"name": "Start", "scene": "Intro", "transitionSeconds": 2.0,
                          "trigger": {"kind": "Manual"}, "restartMotionPhase": true}]}],
  "osc": {"inPort": 9000, "out": {"host": "127.0.0.1", "port": 9001, "enabled": false}},
  "midi": {"mappings": []},
  "clock": {"kind": "Internal", "bpm": 120, "beatsPerBar": 4}
})json";

std::filesystem::path tempDir() {
    auto dir = std::filesystem::temp_directory_path() / "spacer_projectfile_tests";
    std::filesystem::create_directories(dir);
    return dir;
}

}  // namespace

TEST_CASE("The SPEC §15 example parses") {
    auto r = projectfile::parse(kSpecExample);
    REQUIRE(r.hasValue());
    const Project& p = r->project;
    REQUIRE(p.name == "Dome show");
    REQUIRE(p.layout.speakers.size() == 2);
    REQUIRE(p.layout.speakers[0].position.distance == 4.2f);
    REQUIRE(p.devices.outputDeviceName == "Spacer Aggregate");
    REQUIRE(p.ambisonics.order == 3);
    REQUIRE(p.sources.size() == 1);
    const SourceSpec& s = p.sources[0];
    REQUIRE(s.kind() == SourceKind::VirtualInput);
    REQUIRE(std::get<VirtualInputSpec>(s.input.value).right == 1);
    REQUIRE(s.stereo);
    REQUIRE(s.gainDb == -6.0f);
    REQUIRE(s.spatializer == SpatializerKind::Vbap);
    const auto& orbit = std::get<OrbitMotion>(s.motion.value);
    REQUIRE(orbit.period == Period::beats(16));
    REQUIRE(orbit.centre == Position{0, 20, 0});
    REQUIRE(p.scenes.size() == 1);
    REQUIRE(p.cueLists[0].cues[0].sceneName == "Intro");
    REQUIRE(p.cueLists[0].cues[0].restartMotionPhase);
    REQUIRE(p.osc.out.port == 9001);
    REQUIRE(p.clock.bpm == 120.0);
    REQUIRE(r->unknownKeys.empty());
    REQUIRE_FALSE(hasErrors(validate(p)));
}

TEST_CASE("serialize -> parse is lossless for a full project") {
    const Project original = makeFullProject();
    auto r = projectfile::parse(projectfile::serialize(original));
    REQUIRE(r.hasValue());
    REQUIRE(r->project == original);
    REQUIRE(r->unknownKeys.empty());
}

TEST_CASE("save and load a file; no temp file left behind") {
    const auto path = tempDir() / "roundtrip.spacer";
    const Project original = makeFullProject();
    REQUIRE_FALSE(projectfile::save(original, path).has_value());
    REQUIRE_FALSE(std::filesystem::exists(path.string() + ".tmp"));
    auto r = projectfile::load(path);
    REQUIRE(r.hasValue());
    REQUIRE(r->project == original);

    // Overwrite works.
    Project changed = original;
    changed.name = "Changed";
    REQUIRE_FALSE(projectfile::save(changed, path).has_value());
    REQUIRE(projectfile::load(path)->project.name == "Changed");
}

TEST_CASE("save to an unwritable location reports an error") {
    const auto r = projectfile::save(Project{}, tempDir() / "no-such-dir" / "x.spacer");
    REQUIRE(r.has_value());
    REQUIRE_FALSE(r->message.empty());
}

TEST_CASE("load of a missing file is an error") {
    auto r = projectfile::load(tempDir() / "does-not-exist.spacer");
    REQUIRE_FALSE(r.hasValue());
    REQUIRE(r.error().message.find("cannot open") != std::string::npos);
}

TEST_CASE("schemaVersion is checked") {
    SECTION("missing") {
        REQUIRE_FALSE(projectfile::parse(R"({"name":"x"})").hasValue());
    }
    SECTION("not a number") {
        REQUIRE_FALSE(projectfile::parse(R"({"schemaVersion":"1"})").hasValue());
    }
    SECTION("zero / negative") {
        REQUIRE_FALSE(projectfile::parse(R"({"schemaVersion":0})").hasValue());
        REQUIRE_FALSE(projectfile::parse(R"({"schemaVersion":-3})").hasValue());
    }
    SECTION("newer than supported gives a clear message") {
        auto r = projectfile::parse(R"({"schemaVersion":2})");
        REQUIRE_FALSE(r.hasValue());
        REQUIRE(r.error().message.find("newer version") != std::string::npos);
    }
    SECTION("current version with only the version key loads with defaults") {
        auto r = projectfile::parse(R"({"schemaVersion":1})");
        REQUIRE(r.hasValue());
        REQUIRE(r->project == Project{});
    }
}

TEST_CASE("Unknown keys are ignored on load and reported") {
    Json j = Project{};
    j["futureThing"] = 1;
    j["clock"]["swing"] = 0.5;
    j["sources"] = Json::array({Json(SourceSpec{})});
    j["sources"][0]["id"] = 4;
    j["sources"][0]["input"]["extra"] = true;
    j["sources"][0]["motion"]["wibble"] = "x";
    auto r = projectfile::parse(j.dump());
    REQUIRE(r.hasValue());
    REQUIRE(r->unknownKeys.size() == 4);
    REQUIRE(r->unknownKeys[0] == "clock.swing");
    REQUIRE(r->unknownKeys[1] == "futureThing");
    REQUIRE(r->unknownKeys[2] == "sources[0].input.extra");
    REQUIRE(r->unknownKeys[3] == "sources[0].motion.wibble");
}

TEST_CASE("Invalid inputs produce errors, not crashes") {
    const char* inputs[] = {
        "",
        "   ",
        "{",
        "null",
        "[]",
        "42",
        "\"hi\"",
        R"({"schemaVersion":1,"layout":[]})",
        R"({"schemaVersion":1,"sources":[null]})",
        R"({"schemaVersion":1,"sources":[{"id":1,"kind":"Generator","input":{"type":"laser"}}]})",
        R"({"schemaVersion":1,"cueLists":[{"cues":[{"trigger":{"kind":"Sometimes"}}]}]})",
        R"({"schemaVersion":1,"scenes":[{"sources":[{"kind":"File"}]}]})",
        R"({"schemaVersion":1,"midi":{"mappings":[{"type":"smoke"}]}})",
        R"({"schemaVersion":1,"sources":[{"id":1,"kind":"File","input":{"path":5}}]})",
        R"({"schemaVersion":1,"sources":[{"id":1,"kind":"File","modulators":[{"source":{"kind":"x"}}]}]})",
        R"({"schemaVersion":1,"sources":[{"id":1,"kind":"File","inserts":[{"params":{"a":1}}]}]})",
        "\xff\xfe\x00garbage",
    };
    for (const char* text : inputs) {
        INFO(text);
        auto r = projectfile::parse(std::string_view(text, std::char_traits<char>::length(text)));
        REQUIRE_FALSE(r.hasValue());
        REQUIRE_FALSE(r.error().message.empty());
    }
}

TEST_CASE("Every truncation of a valid file is handled") {
    const std::string text = projectfile::serialize(makeFullProject());
    int accepted = 0;
    for (std::size_t n = 0; n < text.size(); n += 7) {
        auto r = projectfile::parse(std::string_view(text.data(), n));
        if (r.hasValue()) ++accepted;
    }
    REQUIRE(accepted == 0);  // a truncated JSON document is never valid
}

TEST_CASE("Single-value mutations of a valid file never crash") {
    const Json base = makeFullProject();
    // Replace each top-level and second-level value with something of the wrong type.
    for (auto it = base.begin(); it != base.end(); ++it) {
        for (const Json& bogus :
             {Json(nullptr), Json("text"), Json(-1), Json(true), Json::array(), Json::object()}) {
            Json j = base;
            j[it.key()] = bogus;
            (void)projectfile::parse(j.dump());
            if (it->is_object()) {
                for (auto in = it->begin(); in != it->end(); ++in) {
                    Json k = base;
                    k[it.key()][in.key()] = bogus;
                    (void)projectfile::parse(k.dump());
                }
            }
        }
    }
    SUCCEED();
}
