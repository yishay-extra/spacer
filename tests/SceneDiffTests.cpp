#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/SceneDiff.h"

using Catch::Approx;
using namespace spacer;

namespace {

Layout twoSpeakers() {
    Layout l;
    l.name = "two";
    for (int i = 1; i <= 2; ++i) {
        Speaker s;
        s.id = i;
        s.position = {i == 1 ? -45.0f : 45.0f, 0.0f, 2.0f};
        s.outputChannel = i - 1;
        l.speakers.push_back(s);
    }
    return l;
}

SourceSpec manualSource(int id, Position p = {0, 0, 2}) {
    SourceSpec s;
    s.id = id;
    s.name = "s" + std::to_string(id);
    s.motion.value = ManualMotion{p, 20.0f};
    return s;
}

Scene sceneWith(std::vector<SourceSpec> sources) {
    Scene s;
    s.name = "A";
    s.sources = std::move(sources);
    return s;
}

bool has(const SceneDiff& d, RampTarget t, int id) {
    return std::any_of(d.ramps.begin(), d.ramps.end(),
                       [&](const Ramp& r) { return r.target == t && r.id == id; });
}

const Ramp& get(const SceneDiff& d, RampTarget t, int id) {
    return *std::find_if(d.ramps.begin(), d.ramps.end(),
                         [&](const Ramp& r) { return r.target == t && r.id == id; });
}

/// Apply every ramp's final value to a scene (what the engine converges to).
void applyRamps(Scene& scene, const SceneDiff& diff, const Layout& layout) {
    for (const Ramp& r : diff.ramps) {
        SourceSpec* src = nullptr;
        for (auto& s : scene.sources)
            if (s.id == r.id) src = &s;
        SpeakerChainSpec* chain = nullptr;
        for (const auto& sp : layout.speakers) {
            if (sp.id != r.id) continue;
            bool found = false;
            for (auto& c : scene.speakerChains)
                if (c.speakerId == sp.id) {
                    chain = &c;
                    found = true;
                }
            if (!found) {
                scene.speakerChains.push_back(effectiveChain(scene, sp));
                chain = &scene.speakerChains.back();
            }
        }
        switch (r.target) {
            case RampTarget::SourceGainDb:
                src->gainDb = r.to;
                break;
            case RampTarget::SourceMute:
                src->mute = r.to > 0.5f;
                break;
            case RampTarget::SourceSpread:
                src->spread = r.to;
                break;
            case RampTarget::SourceWidth:
                src->width = r.to;
                break;
            case RampTarget::SourceDistanceK:
                src->distance.k = r.to;
                break;
            case RampTarget::SourceDistanceRMin:
                src->distance.rMin = r.to;
                break;
            case RampTarget::SourceDistanceQ:
                src->distance.q = r.to;
                break;
            case RampTarget::SourcePosition: {
                MotionSpec& m = (r.channel == 1 && src->motionRight) ? *src->motionRight : src->motion;
                std::get<ManualMotion>(m.value).position = r.toPosition;
                break;
            }
            case RampTarget::SpeakerTrimDb:
                chain->trimDb = r.to;
                break;
            case RampTarget::SpeakerMute:
                chain->mute = r.to > 0.5f;
                break;
            case RampTarget::SpeakerUserDelayMs:
                chain->userDelayMs = r.to;
                break;
            case RampTarget::SpeakerPolarity:
                chain->polarityInverted = r.to > 0.5f;
                break;
            case RampTarget::SpeakerEqLowFreq:
                chain->eq.lowFreq = r.to;
                break;
            case RampTarget::SpeakerEqLowGainDb:
                chain->eq.lowGainDb = r.to;
                break;
            case RampTarget::SpeakerEqMidFreq:
                chain->eq.midFreq = r.to;
                break;
            case RampTarget::SpeakerEqMidGainDb:
                chain->eq.midGainDb = r.to;
                break;
            case RampTarget::SpeakerEqMidQ:
                chain->eq.midQ = r.to;
                break;
            case RampTarget::SpeakerEqHighFreq:
                chain->eq.highFreq = r.to;
                break;
            case RampTarget::SpeakerEqHighGainDb:
                chain->eq.highGainDb = r.to;
                break;
            case RampTarget::MasterGainDb:
                scene.master.gainDb = r.to;
                break;
        }
    }
}

}  // namespace

TEST_CASE("SceneDiff: identical scenes have an empty diff") {
    const Scene a = sceneWith({manualSource(1), manualSource(2)});
    CHECK(diffScenes(a, a, twoSpeakers()).empty());
}

TEST_CASE("SceneDiff: continuous source parameters become ramps with from/to") {
    Scene a = sceneWith({manualSource(1)});
    Scene b = a;
    b.sources[0].gainDb = -12.0f;
    b.sources[0].spread = 0.5f;
    b.sources[0].mute = true;
    b.sources[0].distance.k = 2.0f;
    std::get<ManualMotion>(b.sources[0].motion.value).position = {90, 10, 3};
    b.master.gainDb = -3.0f;

    const SceneDiff d = diffScenes(a, b, twoSpeakers());
    CHECK_FALSE(d.structural());
    CHECK(d.ramps.size() == 6);
    CHECK(get(d, RampTarget::SourceGainDb, 1).from == 0.0f);
    CHECK(get(d, RampTarget::SourceGainDb, 1).to == -12.0f);
    CHECK(get(d, RampTarget::SourceSpread, 1).to == 0.5f);
    CHECK(get(d, RampTarget::SourceMute, 1).to == 1.0f);
    CHECK(get(d, RampTarget::SourceDistanceK, 1).to == 2.0f);
    const Ramp& pos = get(d, RampTarget::SourcePosition, 1);
    CHECK(pos.fromPosition == Position{0, 0, 2});
    CHECK(pos.toPosition == Position{90, 10, 3});
    CHECK(get(d, RampTarget::MasterGainDb, 0).to == -3.0f);
    CHECK_FALSE(has(d, RampTarget::SourceWidth, 1));
}

TEST_CASE("SceneDiff: speaker chains compare against layout defaults when a scene has no entry") {
    Layout layout = twoSpeakers();
    layout.speakers[0].trimDb = -2.0f;
    Scene a = sceneWith({});
    Scene b = a;
    SpeakerChainSpec c;
    c.speakerId = 1;
    c.trimDb = -2.0f;  // same as the layout default: no change
    b.speakerChains.push_back(c);
    CHECK(diffScenes(a, b, layout).empty());

    b.speakerChains[0].trimDb = -6.0f;
    b.speakerChains[0].eq.midGainDb = 3.0f;
    b.speakerChains[0].mute = true;
    b.speakerChains[0].polarityInverted = true;
    b.speakerChains[0].userDelayMs = 4.0f;
    const SceneDiff d = diffScenes(a, b, layout);
    CHECK(get(d, RampTarget::SpeakerTrimDb, 1).from == -2.0f);
    CHECK(get(d, RampTarget::SpeakerTrimDb, 1).to == -6.0f);
    CHECK(has(d, RampTarget::SpeakerEqMidGainDb, 1));
    CHECK(has(d, RampTarget::SpeakerMute, 1));
    CHECK(has(d, RampTarget::SpeakerPolarity, 1));
    CHECK(has(d, RampTarget::SpeakerUserDelayMs, 1));
    CHECK_FALSE(has(d, RampTarget::SpeakerTrimDb, 2));
}

TEST_CASE("SceneDiff: discrete changes are structural, not ramps") {
    const Layout layout = twoSpeakers();
    const Scene a =
        sceneWith({manualSource(1), manualSource(2), manualSource(3), manualSource(4), manualSource(5)});

    Scene b = a;
    b.sources[0].spatializer = SpatializerKind::Dbap;               // spatializer kind
    b.sources[1].motion.value = OrbitMotion{};                      // motion kind
    b.sources[2].inserts.push_back(PluginSlot{"AU:x", "", {}});     // insert added
    b.sources[3].input.value = GeneratorSpec{GeneratorKind::Pink};  // input
    b.sources.erase(b.sources.begin() + 4);                         // removed
    b.sources.push_back(manualSource(6));                           // added

    const SceneDiff d = diffScenes(a, b, layout);
    CHECK(d.structural());
    CHECK(d.replacedSources == std::vector<int>{1, 2, 3, 4});
    CHECK(d.removedSources == std::vector<int>{5});
    CHECK(d.addedSources == std::vector<int>{6});
    CHECK(d.ramps.empty());
}

TEST_CASE("SceneDiff: a changed source is replaced and does not also ramp") {
    Scene a = sceneWith({manualSource(1)});
    Scene b = a;
    b.sources[0].spatializer = SpatializerKind::Dbap;
    b.sources[0].gainDb = -6.0f;
    const SceneDiff d = diffScenes(a, b, twoSpeakers());
    CHECK(d.replacedSources == std::vector<int>{1});
    CHECK(d.ramps.empty());
}

TEST_CASE("SceneDiff: renaming a source is not a change") {
    Scene a = sceneWith({manualSource(1)});
    Scene b = a;
    b.sources[0].name = "renamed";
    CHECK(diffScenes(a, b, twoSpeakers()).empty());
}

TEST_CASE("SceneDiff: linkedPoints ramps both points") {
    SourceSpec s = manualSource(1);
    s.stereo = true;
    s.linkedPoints = true;
    s.motionRight = MotionSpec{ManualMotion{{30, 0, 2}, 20.0f}};
    Scene a = sceneWith({s});
    Scene b = a;
    std::get<ManualMotion>(b.sources[0].motion.value).position = {-30, 0, 2};
    std::get<ManualMotion>(b.sources[0].motionRight->value).position = {60, 0, 2};
    const SceneDiff d = diffScenes(a, b, twoSpeakers());
    REQUIRE(d.ramps.size() == 2);
    CHECK(d.ramps[0].channel == 0);
    CHECK(d.ramps[1].channel == 1);
}

TEST_CASE("SceneDiff: intermediate scene has target structure and current continuous values") {
    const Layout layout = twoSpeakers();
    Scene a = sceneWith({manualSource(1, {0, 0, 2}), manualSource(2), manualSource(3)});
    a.sources[0].gainDb = -3.0f;
    a.master.gainDb = -1.0f;

    Scene b = sceneWith({manualSource(1, {45, 0, 2}), manualSource(2), manualSource(4)});
    b.sources[0].gainDb = -20.0f;
    b.sources[1].spatializer = SpatializerKind::Dbap;  // replaced: taken whole from target
    b.sources[1].gainDb = -9.0f;
    b.master.gainDb = -6.0f;
    SpeakerChainSpec c;
    c.speakerId = 2;
    c.trimDb = -4.0f;
    b.speakerChains.push_back(c);

    const SceneDiff d = diffScenes(a, b, layout);
    const Scene mid = intermediateScene(a, b, d, layout);

    REQUIRE(mid.sources.size() == 3);       // 3 is gone, 4 is in
    CHECK(mid.sources[0].gainDb == -3.0f);  // persists: current value
    CHECK(std::get<ManualMotion>(mid.sources[0].motion.value).position == Position{0, 0, 2});
    CHECK(mid.sources[1].spatializer == SpatializerKind::Dbap);  // replaced: target whole
    CHECK(mid.sources[1].gainDb == -9.0f);
    CHECK(mid.sources[2].id == 4);  // added
    CHECK(mid.master.gainDb == -1.0f);
    REQUIRE(mid.speakerChains.size() == 1);
    CHECK(mid.speakerChains[0].trimDb == 0.0f);  // current continuous value
}

TEST_CASE("SceneDiff: intermediate scene plus the ramps' targets equals the target") {
    const Layout layout = twoSpeakers();
    Scene a = sceneWith({manualSource(1), manualSource(2)});
    Scene b = sceneWith({manualSource(1, {90, 5, 3}), manualSource(2), manualSource(3)});
    b.sources[0].gainDb = -7.0f;
    b.sources[0].spread = 0.3f;
    b.sources[0].width = 90.0f;
    b.sources[0].distance = {2.0f, 1.0f, 0.5f};
    b.sources[1].spatializer = SpatializerKind::Dbap;
    b.master.gainDb = -2.0f;
    SpeakerChainSpec c;
    c.speakerId = 1;
    c.trimDb = -3.0f;
    c.eq.highGainDb = -4.0f;
    c.eq.lowFreq = 120.0f;
    b.speakerChains.push_back(c);

    const SceneDiff d = diffScenes(a, b, layout);
    Scene mid = intermediateScene(a, b, d, layout);
    applyRamps(mid, d, layout);

    CHECK(mid.sources == b.sources);
    CHECK(mid.master == b.master);
    // The target's chain list may omit unchanged speakers; compare effective values.
    for (const auto& sp : layout.speakers) CHECK(effectiveChain(mid, sp) == effectiveChain(b, sp));
}

TEST_CASE("SceneDiff: speaker insert changes are reported") {
    const Layout layout = twoSpeakers();
    Scene a = sceneWith({});
    Scene b = a;
    SpeakerChainSpec c;
    c.speakerId = 2;
    c.inserts.push_back(PluginSlot{"AU:y", "", {}});
    b.speakerChains.push_back(c);
    const SceneDiff d = diffScenes(a, b, layout);
    CHECK(d.speakerInsertChanges == std::vector<int>{2});
    CHECK(d.structural());
}

TEST_CASE("Scene capture / apply / find round trip on a project") {
    Project p;
    p.layout = twoSpeakers();
    p.sources = {manualSource(1), manualSource(2)};
    p.master.gainDb = -5.0f;
    const Scene snap = captureScene(p, "Intro");
    CHECK(snap.name == "Intro");

    Project q;
    q.layout = p.layout;
    q.scenes.push_back(snap);
    applyScene(q, snap);
    CHECK(q.sources == p.sources);
    CHECK(q.master == p.master);
    CHECK(findScene(q, "Intro") != nullptr);
    CHECK(findScene(q, "nope") == nullptr);
}
