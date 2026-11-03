#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>

#include "EngineTestUtils.h"
#include "control/CueEngine.h"
#include "control/InstallationMode.h"
#include "engine/BufferInput.h"
#include "engine/GraphBuilder.h"

using Catch::Approx;
using namespace spacer;
using namespace spacer::testing;

namespace {

constexpr double kSr = 48000.0;
constexpr int kBlock = 128;

GraphBuilder::InputFactory dcInputs() {
    return [](const SourceSpec&) -> std::unique_ptr<SourceInput> {
        juce::AudioBuffer<float> b(1, 48000 * 20);
        for (int i = 0; i < b.getNumSamples(); ++i) b.setSample(0, i, 0.5f);
        return std::make_unique<BufferInput>(std::move(b));
    };
}

Scene sceneWithGain(const Project& p, const char* name, float gainDb) {
    Scene s = captureScene(p, name);
    s.sources[0].gainDb = gainDb;
    return s;
}

Cue cue(const char* name, const char* scene, float transition, Trigger t = {}) {
    Cue c;
    c.name = name;
    c.sceneName = scene;
    c.transitionSeconds = transition;
    c.trigger = t;
    return c;
}

/// Engine + project with one Direct source on speaker 1 and three scenes at 0 / -12 / -24 dB.
struct Show {
    Project project;
    EngineProcessor engine;
    std::unique_ptr<CueEngine> cues;
    std::vector<float> out;
    double now = 0.0;
    ClockState clock;

    explicit Show(std::vector<Cue> list, bool loop = false, bool playing = false) {
        project = makeProject({makeSpeaker(1, -90.0f, 2.0f, 0), makeSpeaker(2, 90.0f, 2.0f, 1)});
        SourceSpec s = makeSource(1, {}, SpatializerKind::Direct);
        s.directSpeakers = {1};
        project.sources.push_back(s);
        project.scenes = {sceneWithGain(project, "A", 0.0f), sceneWithGain(project, "B", -12.0f),
                          sceneWithGain(project, "C", -24.0f)};
        CueList l;
        l.name = "Show";
        l.cues = std::move(list);
        l.loop = loop;
        project.cueLists.push_back(l);
        GraphBuilder b(project);
        b.setInputFactory(dcInputs());
        engine.setGraphWhileStopped(b.build(kSr, kBlock));
        if (playing) engine.push(cmd::ClockContinue{});
        CueEngine::Config cfg;
        cfg.sampleRate = kSr;
        cfg.maxBlockSize = kBlock;
        cfg.inputFactory = dcInputs();
        cues = std::make_unique<CueEngine>(engine, project, cfg);
    }

    CueTick tick() const {
        CueTick t;
        t.nowSeconds = now;
        t.clock = clock;
        return t;
    }

    /// Runs `seconds` of audio, ticking the cue engine every block like the app's timer would.
    void run(double seconds) {
        juce::AudioBuffer<float> buf(2, kBlock);
        const int blocks = static_cast<int>(seconds * kSr / kBlock);
        for (int i = 0; i < blocks; ++i) {
            float* p[] = {buf.getWritePointer(0), buf.getWritePointer(1)};
            engine.process(nullptr, 0, p, 2, kBlock);
            out.insert(out.end(), p[0], p[0] + kBlock);
            now += kBlock / kSr;
            Feedback f;
            while (engine.popFeedback(f)) {
                if (const auto* c = std::get_if<fb::Clock>(&f)) clock = c->state;
                if (const auto* c = std::get_if<fb::CueFired>(&f)) cues->onEngineCueFired(c->token, tick());
            }
            cues->tick(tick());
            engine.collectGarbage();
        }
    }
    float last() const { return out.back(); }
};

}  // namespace

TEST_CASE("CueEngine: Go recalls the cue's scene with its transition time") {
    Show show({cue("one", "B", 1.0f), cue("two", "C", 0.5f)});
    show.run(0.2);
    REQUIRE(show.last() == Approx(0.5f));
    REQUIRE(show.cues->armFirst(show.tick()));
    CHECK(show.cues->go(show.tick()));
    show.run(1.2);
    CHECK(show.last() == Approx(0.5f * juce::Decibels::decibelsToGain(-12.0f)).margin(1e-4));
    CHECK(show.project.sources[0].gainDb == -12.0f);
    CHECK(show.cues->go(show.tick()));
    show.run(0.7);
    CHECK(show.last() == Approx(0.5f * juce::Decibels::decibelsToGain(-24.0f)).margin(1e-4));
    CHECK_FALSE(show.cues->go(show.tick()));  // end of list
}

TEST_CASE("CueEngine: Back and Jump recall the earlier scene") {
    Show show({cue("one", "B", 0.1f), cue("two", "C", 0.1f)});
    show.cues->armFirst(show.tick());
    show.cues->go(show.tick());
    show.cues->go(show.tick());
    show.run(0.5);
    CHECK(show.cues->back(show.tick()));
    show.run(0.5);
    CHECK(show.last() == Approx(0.5f * juce::Decibels::decibelsToGain(-12.0f)).margin(1e-4));
    CHECK(show.cues->jump(1, show.tick()));
    show.run(0.5);
    CHECK(show.last() == Approx(0.5f * juce::Decibels::decibelsToGain(-24.0f)).margin(1e-4));
}

TEST_CASE("CueEngine: a looping AfterPrevious list cycles by itself") {
    Trigger after{trigger::AfterPrevious{Period::seconds(1.0), 0.0}};
    Show show({cue("a", "B", 0.1f, after), cue("b", "C", 0.1f, after)}, /*loop*/ true);
    show.cues->armFirst(show.tick());
    show.run(3.5);  // cue a at 1 s, b at 2 s, a at 3 s
    CHECK(show.cues->sequencer().current() == 0);
    CHECK(show.last() == Approx(0.5f * juce::Decibels::decibelsToGain(-12.0f)).margin(1e-4));
    show.run(1.0);  // b again at 4 s
    CHECK(show.cues->sequencer().current() == 1);
}

TEST_CASE("CueEngine: AtBeat cue fires in the engine and advances the list") {
    Trigger atBeat{trigger::AtBeat{2, 1.0}};  // beat 4 = 2 s at 120 bpm
    Show show({cue("first", "A", 0.0f), cue("beat", "B", 0.0f, atBeat)}, false, /*playing*/ true);
    show.cues->armFirst(show.tick());
    REQUIRE(show.cues->go(show.tick()));
    show.run(1.9);
    CHECK(show.cues->sequencer().current() == 0);
    CHECK(show.last() == Approx(0.5f));
    show.run(0.5);
    CHECK(show.cues->sequencer().current() == 1);
    CHECK(show.project.sources[0].gainDb == -12.0f);
    CHECK(show.last() == Approx(0.5f * juce::Decibels::decibelsToGain(-12.0f)).margin(1e-4));
}

TEST_CASE("CueEngine: a manual Go cancels the pending beat trigger") {
    Trigger atBeat{trigger::AtBeat{2, 1.0}};
    Show show({cue("first", "A", 0.0f), cue("beat", "B", 0.0f, atBeat), cue("last", "C", 0.0f)}, false, true);
    show.cues->armFirst(show.tick());
    show.cues->go(show.tick());
    show.run(0.5);
    REQUIRE(show.cues->go(show.tick()));  // fires the beat cue early by hand
    show.run(3.0);                        // the beat passes: must not fire again
    CHECK(show.cues->sequencer().current() == 1);
}

TEST_CASE("CueEngine: Osc and Midi triggers advance the list") {
    Show show({cue("a", "B", 0.0f, {trigger::Osc{"/go"}}),
               cue("b", "C", 0.0f, {trigger::Midi{trigger::Midi::Type::Note, 60, 0}})});
    show.cues->armFirst(show.tick());
    CHECK_FALSE(show.cues->onOsc("/nope", show.tick()));
    CHECK(show.cues->onOsc("/go", show.tick()));
    CHECK(show.cues->onMidi(trigger::Midi::Type::Note, 60, 1, show.tick()));
    show.run(0.3);
    CHECK(show.project.sources[0].gainDb == -24.0f);
}

TEST_CASE("CueEngine: a cue naming a missing scene still advances and reports it") {
    Show show({cue("a", "Nope", 0.0f), cue("b", "C", 0.0f)});
    show.cues->armFirst(show.tick());
    CHECK(show.cues->go(show.tick()));
    CHECK_FALSE(show.cues->lastError().empty());
    CHECK(show.cues->go(show.tick()));
    CHECK(show.cues->lastError().empty());
}

TEST_CASE("CueEngine: scene manager recalls, stores and updates scenes") {
    Show show({});
    CHECK(show.cues->recallScene("C", 0.1f, false));
    show.run(0.3);
    CHECK(show.last() == Approx(0.5f * juce::Decibels::decibelsToGain(-24.0f)).margin(1e-4));
    CHECK_FALSE(show.cues->recallScene("Missing", 0.1f, false));

    show.project.sources[0].gainDb = -3.0f;
    show.cues->storeScene("C");  // update
    show.cues->storeScene("D");  // new
    CHECK(show.project.scenes.size() == 4);
    CHECK(findScene(show.project, "C")->sources[0].gainDb == -3.0f);
}

TEST_CASE("Installation mode: settings round trip") {
    InstallationSettings s;
    s.enabled = true;
    s.lastProjectPath = "/tmp/show.spacer";
    s.defaultCueList = "Show";
    s.autosaveSeconds = 15;
    s.suppressDialogs = false;
    const auto parsed = installation::parseSettings(installation::toJsonText(s));
    REQUIRE(parsed);
    CHECK(*parsed == s);
    CHECK_FALSE(installation::parseSettings("[1]"));
    CHECK_FALSE(installation::parseSettings("{"));
}

TEST_CASE("Installation mode: autosave writes a valid project file, only when dirty and due") {
    const auto dir = std::filesystem::temp_directory_path() / "spacer_wp9_autosave";
    std::filesystem::create_directories(dir);
    const auto projectPath = dir / "show.spacer";
    std::filesystem::remove(installation::autosavePath(projectPath));

    Show show({cue("a", "B", 0.0f)});
    Autosaver saver(projectPath, 60.0);
    CHECK(saver.path().filename() == "show.autosave.spacer");
    CHECK_FALSE(saver.tick(0.0, show.project, true));    // first call starts the timer
    CHECK_FALSE(saver.tick(30.0, show.project, true));   // not due
    CHECK_FALSE(saver.tick(61.0, show.project, false));  // due but clean
    CHECK_FALSE(std::filesystem::exists(saver.path()));
    CHECK(saver.tick(61.0, show.project, true));
    REQUIRE(std::filesystem::exists(saver.path()));

    const auto loaded = projectfile::load(saver.path());
    REQUIRE(loaded);
    CHECK(loaded->project == show.project);
    CHECK_FALSE(saver.tick(70.0, show.project, true));  // interval restarted
    std::filesystem::remove_all(dir);
}

TEST_CASE("Installation mode: autoload opens the last project, falling back to its autosave") {
    const auto dir = std::filesystem::temp_directory_path() / "spacer_wp9_autoload";
    std::filesystem::create_directories(dir);
    const auto path = dir / "inst.spacer";
    Show show({cue("a", "B", 0.0f)});
    REQUIRE_FALSE(projectfile::save(show.project, path));

    InstallationSettings s;
    s.enabled = true;
    s.lastProjectPath = path.string();
    bool usedAutosave = true;
    auto loaded = installation::autoload(s, &usedAutosave);
    REQUIRE(loaded);
    CHECK_FALSE(usedAutosave);
    CHECK(loaded->project == show.project);

    // Main file gone: the autosave is used.
    REQUIRE_FALSE(projectfile::save(show.project, installation::autosavePath(path)));
    std::filesystem::remove(path);
    loaded = installation::autoload(s, &usedAutosave);
    REQUIRE(loaded);
    CHECK(usedAutosave);

    std::filesystem::remove(installation::autosavePath(path));
    CHECK_FALSE(installation::autoload(s));
    std::filesystem::remove_all(dir);
}

TEST_CASE("Installation mode: startShow arms the default list and fires its first cue") {
    Show show({cue("a", "B", 0.0f), cue("b", "C", 0.0f)});
    InstallationSettings s;
    s.enabled = true;
    CHECK(installation::startShow(*show.cues, s, show.tick()));
    CHECK(show.cues->sequencer().current() == 0);
    show.run(0.3);
    CHECK(show.project.sources[0].gainDb == -12.0f);

    s.defaultCueList = "Missing";
    CHECK_FALSE(installation::startShow(*show.cues, s, show.tick()));
}

TEST_CASE("CueEngine: refreshArmedList follows edits to the armed list") {
    Show show({cue("one", "A", 0.0f), cue("two", "B", 0.0f), cue("three", "C", 0.0f)});
    CHECK_FALSE(show.cues->refreshArmedList(show.tick()));  // not armed: nothing to do
    REQUIRE(show.cues->arm("Show", show.tick()));
    CHECK_FALSE(show.cues->refreshArmedList(show.tick()));  // unchanged
    REQUIRE(show.cues->go(show.tick()));
    CHECK(show.cues->sequencer().current() == 0);

    // Edit a cue: the armed copy follows and the position is kept.
    show.project.cueLists[0].cues[1].sceneName = "C";
    CHECK(show.cues->refreshArmedList(show.tick()));
    CHECK(show.cues->sequencer().list().cues[1].sceneName == "C");
    CHECK(show.cues->sequencer().current() == 0);
    CHECK(show.cues->sequencer().next() == 1);

    // Rename (same number of lists): still armed, same position.
    show.project.cueLists[0].name = "Renamed";
    CHECK(show.cues->refreshArmedList(show.tick()));
    CHECK(show.cues->armed());
    CHECK(show.cues->armedName() == "Renamed");
    CHECK(show.cues->sequencer().current() == 0);

    // Removed: disarmed.
    show.project.cueLists.clear();
    CHECK(show.cues->refreshArmedList(show.tick()));
    CHECK_FALSE(show.cues->armed());
}

TEST_CASE("CueEngine: recallCount counts every recall that changed the live model") {
    Show show({cue("one", "A", 0.0f), cue("two", "B", 0.0f)});
    REQUIRE(show.cues->arm("Show", show.tick()));
    CHECK(show.cues->recallCount() == 0);
    REQUIRE(show.cues->go(show.tick()));
    CHECK(show.cues->recallCount() == 1);
    // A scene recall outside the cue list does not move the cue index but is counted.
    const int current = show.cues->sequencer().current();
    REQUIRE(show.cues->recallScene("C", 0.0f, false));
    CHECK(show.cues->sequencer().current() == current);
    CHECK(show.cues->recallCount() == 2);
    CHECK_FALSE(show.cues->recallScene("Missing", 0.0f, false));
    CHECK(show.cues->recallCount() == 2);
}

TEST_CASE("CueEngine: setConfig keeps the armed list and position (device change, WP14)") {
    Show show({cue("one", "B", 0.0f), cue("two", "C", 0.0f), cue("three", "A", 0.0f)});
    show.cues->armFirst(show.tick());
    show.cues->go(show.tick());
    show.cues->go(show.tick());
    REQUIRE(show.cues->sequencer().current() == 1);
    CueEngine::Config cfg = show.cues->config();
    cfg.sampleRate = 96000.0;
    cfg.maxBlockSize = 512;
    show.cues->setConfig(cfg, show.tick());
    CHECK(show.cues->armed());
    CHECK(show.cues->sequencer().current() == 1);
    CHECK(show.cues->config().sampleRate == 96000.0);
    CHECK(show.cues->go(show.tick()));
    CHECK(show.cues->sequencer().current() == 2);
}

TEST_CASE("CueEngine: setConfig re-plans a pending AtBeat recall (WP14)") {
    Trigger atBeat{trigger::AtBeat{2, 1.0}};  // beat 4 = 2 s at 120 bpm
    Show show({cue("first", "A", 0.0f), cue("beat", "B", 0.0f, atBeat)}, false, /*playing*/ true);
    show.cues->armFirst(show.tick());
    REQUIRE(show.cues->go(show.tick()));
    show.run(1.0);
    show.cues->setConfig(show.cues->config(), show.tick());
    show.run(1.5);
    CHECK(show.cues->sequencer().current() == 1);
    CHECK(show.last() == Approx(0.5f * juce::Decibels::decibelsToGain(-12.0f)).margin(1e-4));
}

TEST_CASE("CueEngine: onLiveChanged sees every recall's live model (WP14)") {
    Trigger atBeat{trigger::AtBeat{2, 1.0}};
    Show show({cue("one", "C", 0.0f), cue("beat", "B", 0.0f, atBeat)}, false, /*playing*/ true);
    std::vector<float> seen;
    CueEngine::Config cfg = show.cues->config();
    cfg.onLiveChanged = [&](const Project& p) { seen.push_back(p.sources[0].gainDb); };
    show.cues->setConfig(cfg, show.tick());
    show.cues->armFirst(show.tick());
    show.cues->go(show.tick());                // cue: C
    show.cues->recallScene("A", 0.0f, false);  // scene manager / OSC
    show.run(2.5);                             // AtBeat cue fired in the engine: B
    CHECK(seen == std::vector<float>{-24.0f, 0.0f, -12.0f});
}
