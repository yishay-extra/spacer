#include <catch2/catch_test_macros.hpp>

#include "TestModels.h"
#include "core/Validate.h"

using namespace spacer;
using namespace spacer::testing;

namespace {

bool hasIssue(const Issues& issues, const std::string& pathPart,
              Issue::Severity sev = Issue::Severity::Error) {
    for (const auto& i : issues)
        if (i.severity == sev && i.path.find(pathPart) != std::string::npos) return true;
    return false;
}

Project validProject() {
    Project p = makeFullProject();
    return p;
}

}  // namespace

TEST_CASE("A fully populated project is valid") {
    const auto issues = validate(makeFullProject());
    for (const auto& i : issues) INFO(i.path << ": " << i.message);
    REQUIRE_FALSE(hasErrors(issues));
}

TEST_CASE("An empty default project is invalid only because it has no speakers") {
    const auto issues = validate(Project{});
    REQUIRE(hasIssue(issues, "layout.speakers"));
    Project p;
    p.layout = *layoutpresets::load("stereo");
    REQUIRE_FALSE(hasErrors(validate(p)));
}

TEST_CASE("Layout rules (§7)") {
    Layout l = *layoutpresets::load("quad");
    REQUIRE_FALSE(hasErrors(validate(l)));

    SECTION("too few speakers") {
        l.speakers.resize(1);
        REQUIRE(hasIssue(validate(l), "layout.speakers"));
    }
    SECTION("too many speakers") {
        l = *layoutpresets::load("ring16");
        for (int i = 0; i < 17; ++i) {
            Speaker s;
            s.id = 100 + i;
            s.position = {static_cast<float>(i), 0, 2};
            l.speakers.push_back(s);
        }
        REQUIRE(hasIssue(validate(l), "layout.speakers"));
    }
    SECTION("32 speakers are fine") {
        l.speakers.clear();
        for (int i = 0; i < 32; ++i) l.speakers.push_back({i + 1, "s", {i * 11.0f, 0, 2}, 0, 0, false, i});
        REQUIRE_FALSE(hasErrors(validate(l)));
    }
    SECTION("duplicate ids") {
        l.speakers[1].id = l.speakers[0].id;
        REQUIRE(hasIssue(validate(l), "speakers[1].id"));
    }
    SECTION("id < 1") {
        l.speakers[0].id = 0;
        REQUIRE(hasIssue(validate(l), "speakers[0].id"));
    }
    SECTION("duplicate output channels") {
        l.speakers[2].outputChannel = l.speakers[0].outputChannel;
        REQUIRE(hasIssue(validate(l), "speakers[2].outputChannel"));
    }
    SECTION("several unassigned (-1) channels are allowed") {
        l.speakers[0].outputChannel = -1;
        l.speakers[1].outputChannel = -1;
        REQUIRE_FALSE(hasErrors(validate(l)));
    }
    SECTION("channel below -1") {
        l.speakers[0].outputChannel = -2;
        REQUIRE(hasIssue(validate(l), "outputChannel"));
    }
    SECTION("distance must be > 0") {
        l.speakers[0].position.distance = 0.0f;
        REQUIRE(hasIssue(validate(l), "position.distance"));
        l.speakers[0].position.distance = -1.0f;
        REQUIRE(hasIssue(validate(l), "position"));
    }
    SECTION("non-finite values") {
        l.speakers[0].position.azimuth = std::numeric_limits<float>::quiet_NaN();
        REQUIRE(hasIssue(validate(l), "position"));
    }
    SECTION("coincident directions only warn") {
        l.speakers[1].position = l.speakers[0].position;
        const auto issues = validate(l);
        REQUIRE_FALSE(hasErrors(issues));
        REQUIRE(hasIssue(issues, "speakers[1]", Issue::Severity::Warning));
    }
    SECTION("an LFE speaker may share a direction without a warning") {
        l.speakers[1].position = l.speakers[0].position;
        l.speakers[1].lfe = true;
        REQUIRE(validate(l).empty());
    }
}

TEST_CASE("Source limits and ids") {
    Project p = validProject();

    SECTION("duplicate source ids") {
        p.sources[1].id = p.sources[0].id;
        REQUIRE(hasIssue(validate(p), "sources[1].id"));
    }
    SECTION("more than 64 sources") {
        p.sources.clear();
        for (int i = 1; i <= 65; ++i) {
            SourceSpec s;
            s.id = i;
            s.input.value = VirtualInputSpec{{i % 64, -1}};
            p.sources.push_back(s);
        }
        REQUIRE(hasIssue(validate(p), "sources"));
        p.sources.pop_back();
        REQUIRE_FALSE(hasErrors(validate(p)));
    }
    SECTION("duplicate names warn") {
        p.sources[1].name = p.sources[0].name;
        const auto issues = validate(p);
        REQUIRE_FALSE(hasErrors(issues));
        REQUIRE(hasIssue(issues, "sources[1].name", Issue::Severity::Warning));
    }
}

TEST_CASE("Source field rules") {
    const Layout layout = *layoutpresets::load("quad");
    SourceSpec s;
    s.id = 1;
    s.input.value = VirtualInputSpec{{0, -1}};
    REQUIRE_FALSE(hasErrors(validate(s, layout)));

    SECTION("virtual channel range") {
        s.input.value = VirtualInputSpec{{64, -1}};
        REQUIRE(hasIssue(validate(s, layout), "input"));
        s.input.value = VirtualInputSpec{{-1, -1}};
        REQUIRE(hasIssue(validate(s, layout), "input"));
        s.input.value = VirtualInputSpec{{63, -1}};
        REQUIRE_FALSE(hasErrors(validate(s, layout)));
    }
    SECTION("stereo flag and channels must agree") {
        s.stereo = true;
        REQUIRE(hasIssue(validate(s, layout), "input"));
        s.input.value = VirtualInputSpec{{0, 64}};
        REQUIRE(hasIssue(validate(s, layout), "input"));
        s.input.value = VirtualInputSpec{{0, 1}};
        REQUIRE_FALSE(hasErrors(validate(s, layout)));
        s.stereo = false;
        REQUIRE(hasIssue(validate(s, layout), "input"));
    }
    SECTION("spread range") {
        s.spread = 1.5f;
        REQUIRE(hasIssue(validate(s, layout), "spread"));
    }
    SECTION("empty file path") {
        s.input.value = FileInputSpec{};
        REQUIRE(hasIssue(validate(s, layout), "input.path"));
    }
    SECTION("generator params") {
        s.input.value = GeneratorSpec{GeneratorKind::Sine, 0.0f, 1.0f};
        REQUIRE(hasIssue(validate(s, layout), "hz"));
        s.input.value = GeneratorSpec{GeneratorKind::Impulse, 0.0f, 0.0f};
        REQUIRE(hasIssue(validate(s, layout), "periodSeconds"));
    }
    SECTION("distance model") {
        s.distance.k = -1.0f;
        s.distance.rMin = 0.0f;
        const auto issues = validate(s, layout);
        REQUIRE(hasIssue(issues, "distance.k"));
        REQUIRE(hasIssue(issues, "distance.rMin"));
    }
    SECTION("Direct needs existing speakers") {
        s.spatializer = SpatializerKind::Direct;
        REQUIRE(hasIssue(validate(s, layout), "directSpeakers"));
        s.directSpeakers = {1, 99};
        REQUIRE(hasIssue(validate(s, layout), "directSpeakers"));
        s.directSpeakers = {1, 2};
        REQUIRE_FALSE(hasErrors(validate(s, layout)));
    }
    SECTION("linkedPoints needs stereo") {
        s.linkedPoints = true;
        REQUIRE(hasIssue(validate(s, layout), "linkedPoints"));
    }
    SECTION("insert and modulator caps") {
        s.inserts.assign(5, makeSlot());
        s.modulators.assign(5, allModulators()[1]);
        const auto issues = validate(s, layout);
        REQUIRE(hasIssue(issues, "inserts"));
        REQUIRE(hasIssue(issues, "modulators"));
        s.inserts.assign(4, makeSlot());
        s.modulators.assign(4, allModulators()[1]);
        REQUIRE_FALSE(hasErrors(validate(s, layout)));
    }
    SECTION("empty plugin id") {
        s.inserts = {PluginSlot{}};
        REQUIRE(hasIssue(validate(s, layout), "pluginId"));
    }
    SECTION("modulator target and OSC address") {
        s.modulators = {ModulatorSpec{OscSource{"no-slash"}, "", 1.0f, 0.0f}};
        const auto issues = validate(s, layout);
        REQUIRE(hasIssue(issues, "target"));
        REQUIRE(hasIssue(issues, "address"));
    }
}

TEST_CASE("Motion rules") {
    const Layout layout = *layoutpresets::load("quad");
    SourceSpec s;
    s.id = 1;
    s.input.value = VirtualInputSpec{{0, -1}};

    SECTION("period must be > 0") {
        s.motion = MotionSpec{OrbitMotion{.period = Period::beats(0)}};
        REQUIRE(hasIssue(validate(s, layout), "motion.period"));
    }
    SECTION("orbit direction and radius") {
        OrbitMotion o;
        o.direction = 0;
        o.radius = -1.0f;
        s.motion = MotionSpec{o};
        const auto issues = validate(s, layout);
        REQUIRE(hasIssue(issues, "direction"));
        REQUIRE(hasIssue(issues, "radius"));
    }
    SECTION("path needs two points") {
        s.motion = MotionSpec{PathMotion{{{0, 1, 0}}}};
        REQUIRE(hasIssue(validate(s, layout), "points"));
    }
    SECTION("step sequence") {
        s.motion = MotionSpec{StepSequenceMotion{}};
        REQUIRE(hasIssue(validate(s, layout), "steps"));
        StepSequenceMotion seq;
        seq.steps = {Step{77, Period::beats(1), StepTransition::Jump, Period::beats(1), 2.0f}};
        s.motion = MotionSpec{seq};
        const auto issues = validate(s, layout);
        REQUIRE(hasIssue(issues, "target"));
        REQUIRE(hasIssue(issues, "probability"));
    }
    SECTION("random walk bounds") {
        RandomWalkMotion w;
        w.bounds = BoxBounds{{1, 1, 1}, {0, 0, 0}};
        s.motion = MotionSpec{w};
        REQUIRE(hasIssue(validate(s, layout), "bounds"));
        w.bounds = SphereBounds{{0, 0, 0}, 0.0f};
        s.motion = MotionSpec{w};
        REQUIRE(hasIssue(validate(s, layout), "bounds"));
    }
    SECTION("positions out of range") {
        s.motion = MotionSpec{ManualMotion{{0, 120, 1}, 20}};
        REQUIRE(hasIssue(validate(s, layout), "position"));
    }
    SECTION("all default motions are valid") {
        for (const auto& m : allMotions()) {
            s.motion = m;
            for (const auto& i : validate(s, layout)) INFO(i.path << ": " << i.message);
            REQUIRE_FALSE(hasErrors(validate(s, layout)));
        }
    }
}

TEST_CASE("Cue references resolve (§11)") {
    Project p = validProject();

    SECTION("unknown scene") {
        p.cueLists[0].cues[2].sceneName = "Nope";
        REQUIRE(hasIssue(validate(p), "cueLists[0].cues[2].scene"));
    }
    SECTION("duplicate and empty scene names") {
        p.scenes[1].name = p.scenes[0].name;
        REQUIRE(hasIssue(validate(p), "scenes[1].name"));
        p.scenes[1].name = "";
        REQUIRE(hasIssue(validate(p), "scenes[1].name"));
    }
    SECTION("duplicate cue list names") {
        p.cueLists[1].name = p.cueLists[0].name;
        REQUIRE(hasIssue(validate(p), "cueLists[1].name"));
    }
    SECTION("negative transition") {
        p.cueLists[0].cues[0].transitionSeconds = -1.0f;
        REQUIRE(hasIssue(validate(p), "transitionSeconds"));
    }
    SECTION("trigger ranges") {
        p.cueLists[0].cues[1].trigger = Trigger{trigger::AtBeat{0, 1.0}};
        REQUIRE(hasIssue(validate(p), "trigger.bar"));
        p.cueLists[0].cues[1].trigger = Trigger{trigger::AtBeat{1, 5.0}};  // 4/4: beat < 5
        REQUIRE(hasIssue(validate(p), "trigger.beat"));
        p.cueLists[0].cues[1].trigger = Trigger{trigger::Midi{trigger::Midi::Type::Note, 128, 0}};
        REQUIRE(hasIssue(validate(p), "trigger.number"));
        p.cueLists[0].cues[1].trigger = Trigger{trigger::Osc{"cue"}};
        REQUIRE(hasIssue(validate(p), "trigger.address"));
    }
    SECTION("scene contents are validated too") {
        p.scenes[0].sources[0].spread = 3.0f;
        REQUIRE(hasIssue(validate(p), "scenes[0].sources[0].spread"));
        p.scenes[0].sources[0].spread = 0.0f;
        p.scenes[0].speakerChains[0].speakerId = 999;
        REQUIRE(hasIssue(validate(p), "scenes[0].speakerChains[0].speakerId"));
    }
    SECTION("duplicate speaker chains") {
        p.speakerChains[1].speakerId = p.speakerChains[0].speakerId;
        REQUIRE(hasIssue(validate(p), "speakerChains[1]"));
    }
}

TEST_CASE("Project setting ranges") {
    Project p = validProject();
    SECTION("ambisonic order") {
        p.ambisonics.order = 0;
        REQUIRE(hasIssue(validate(p), "ambisonics.order"));
        p.ambisonics.order = 6;
        REQUIRE(hasIssue(validate(p), "ambisonics.order"));
    }
    SECTION("device") {
        p.devices.bufferSize = 16;
        p.devices.sampleRate = 0.0;
        p.devices.speedOfSound = 0.0f;
        const auto issues = validate(p);
        REQUIRE(hasIssue(issues, "devices.bufferSize"));
        REQUIRE(hasIssue(issues, "devices.sampleRate"));
        REQUIRE(hasIssue(issues, "devices.speedOfSound"));
    }
    SECTION("clock") {
        p.clock.bpm = 10.0;
        p.clock.beatsPerBar = 0;
        const auto issues = validate(p);
        REQUIRE(hasIssue(issues, "clock.bpm"));
        REQUIRE(hasIssue(issues, "clock.beatsPerBar"));
    }
    SECTION("osc ports") {
        p.osc.inPort = 0;
        p.osc.out.port = 70000;
        const auto issues = validate(p);
        REQUIRE(hasIssue(issues, "osc.inPort"));
        REQUIRE(hasIssue(issues, "osc.out.port"));
    }
    SECTION("midi mappings") {
        p.midi.mappings[0].parameterPath = "";
        p.midi.mappings[1].min = 0.0f;  // log curve
        const auto issues = validate(p);
        REQUIRE(hasIssue(issues, "midi.mappings[0].parameterPath"));
        REQUIRE(hasIssue(issues, "midi.mappings[1]"));
    }
}
