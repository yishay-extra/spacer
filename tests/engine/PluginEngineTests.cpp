// Plugin inserts in the running engine (WP10): latency compensation in an
// offline render, insert commands, scene recall of insert state, and
// instances surviving graph swaps.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <map>
#include <memory>
#include <vector>

#include "EngineTestUtils.h"
#include "TestPlugins.h"
#include "core/Json.h"
#include "engine/AllocationGuard.h"
#include "engine/BufferInput.h"
#include "engine/EngineProcessor.h"
#include "engine/OfflineRenderer.h"
#include "engine/PluginHost.h"
#include "engine/PluginState.h"
#include "engine/RecallPlanner.h"

using Catch::Approx;
using namespace spacer;
using namespace spacer::testing;

namespace {

constexpr double kSr = 48000.0;
constexpr int kBlock = 128;

struct TempDir {
    juce::File dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                         .getNonexistentChildFile("spacer-plugin-engine", "", false);
    TempDir() { dir.createDirectory(); }
    ~TempDir() { dir.deleteRecursively(); }
};

struct Host {
    TempDir tmp;
    PluginHost host{[this] {
                        PluginHost::Options o;
                        o.cacheDirectory = tmp.dir;
                        return o;
                    }(),
                    addTestFormat};
};

PluginSlot slot(const juce::String& kind, std::map<int, float> params = {}) {
    PluginSlot s;
    s.pluginId = TestPluginFormat::id(kind).toStdString();
    s.params = std::move(params);
    return s;
}

SourceSpec directSource(int id, std::vector<int> speakers) {
    SourceSpec s = makeSource(id, {}, SpatializerKind::Direct);
    s.directSpeakers = std::move(speakers);
    return s;
}

/// Two speakers at the same distance on channels 0 / 1.
Project twoChannels(std::vector<SourceSpec> sources) {
    return makeProject({makeSpeaker(1, -90.0f, 2.0f, 0), makeSpeaker(2, 90.0f, 2.0f, 1)}, std::move(sources));
}

juce::AudioBuffer<float> impulseAt(int at, int length = 4800) {
    juce::AudioBuffer<float> b(1, length);
    b.clear();
    b.setSample(0, at, 0.5f);
    return b;
}

juce::AudioBuffer<float> dc(float value, int length = 48000 * 6) {
    juce::AudioBuffer<float> b(1, length);
    for (int i = 0; i < length; ++i) b.setSample(0, i, value);
    return b;
}

int peakIndex(const juce::AudioBuffer<float>& b, int channel) {
    int best = 0;
    for (int i = 1; i < b.getNumSamples(); ++i)
        if (std::abs(b.getSample(channel, i)) > std::abs(b.getSample(channel, best))) best = i;
    return best;
}

SpeakerChainSpec chainWith(int speakerId, std::vector<PluginSlot> inserts) {
    SpeakerChainSpec c;
    c.speakerId = speakerId;
    c.inserts = std::move(inserts);
    return c;
}

/// Engine + live project + plugin host + recorder.
struct Rig {
    Host plugins;
    EngineProcessor proc;
    Project project;
    std::vector<std::vector<float>> rec{2};

    GraphBuilder::InputFactory inputs() const {
        return [](const SourceSpec&) -> std::unique_ptr<SourceInput> {
            return std::make_unique<BufferInput>(dc(0.25f));
        };
    }
    std::unique_ptr<Graph> build() {
        GraphBuilder b(project);
        b.setInputFactory(inputs());
        b.setPluginHost(&plugins.host);
        return b.build(kSr, kBlock);
    }
    explicit Rig(Project p) : project(std::move(p)) { proc.setGraphWhileStopped(build()); }

    void run(int samples) {
        juce::AudioBuffer<float> out(2, kBlock);
        for (int done = 0; done < samples; done += kBlock) {
            float* ptrs[] = {out.getWritePointer(0), out.getWritePointer(1)};
            proc.process(nullptr, 0, ptrs, 2, kBlock);
            for (int c = 0; c < 2; ++c)
                rec[static_cast<std::size_t>(c)].insert(rec[static_cast<std::size_t>(c)].end(), ptrs[c],
                                                        ptrs[c] + kBlock);
            Feedback f;
            while (proc.popFeedback(f)) {
            }
            proc.collectGarbage();
        }
    }
    int pos() const { return static_cast<int>(rec[0].size()); }
    float at(int ch, int i) const { return rec[static_cast<std::size_t>(ch)][static_cast<std::size_t>(i)]; }
    float last(int ch) const { return rec[static_cast<std::size_t>(ch)].back(); }
    float maxStep(int ch, int from) const {
        float m = 0.0f;
        for (int i = std::max(1, from); i < pos(); ++i) m = std::max(m, std::abs(at(ch, i) - at(ch, i - 1)));
        return m;
    }

    bool recall(const Scene& target, float seconds) {
        plugins.host.captureState(project);
        RecallRequest r;
        r.project = &project;
        r.target = &target;
        r.transitionSeconds = seconds;
        r.sampleRate = kSr;
        r.maxBlockSize = kBlock;
        r.inputFactory = inputs();
        r.pluginHost = &plugins.host;
        auto plan = buildRecallPlan(r);
        const bool ok = proc.push(cmd::RecallScene{plan.release()});
        applyScene(project, target);
        return ok;
    }

    TestPlugin& plugin(InsertOwner owner, int id, int s) {
        return dynamic_cast<TestPlugin&>(
            proc.currentGraph()->findInserts(owner, id)->chain().plugin(s)->instance());
    }
};

}  // namespace

TEST_CASE("Plugins render: source insert latency is compensated across sources") {
    Host h;
    Project p = twoChannels({directSource(1, {1}), directSource(2, {2})});
    p.sources[0].inserts = {slot("delay64")};
    OfflineRenderOptions options;
    options.pluginHost = &h.host;
    const auto r = OfflineRenderer::render(p, {{1, impulseAt(1000)}, {2, impulseAt(1000)}}, 0.1, options);
    REQUIRE(r);
    const int limiter = static_cast<int>(std::lround(1.5e-3 * kSr));
    CHECK(r->latencySamples == limiter + 64);
    // Both sources reach their speakers at the same sample: the source with no
    // plugin is padded by the other's 64 samples.
    CHECK(peakIndex(r->audio, 0) == 1000 + r->latencySamples);
    CHECK(peakIndex(r->audio, 1) == 1000 + r->latencySamples);
    CHECK(r->audio.getSample(0, 1000 + r->latencySamples) == Approx(0.5f));
    CHECK(r->audio.getSample(1, 1000 + r->latencySamples) == Approx(0.5f));
}

TEST_CASE("Plugins render: speaker insert latency is compensated across speakers") {
    Host h;
    Project p = twoChannels({directSource(1, {1, 2})});
    p.speakerChains = {chainWith(1, {slot("delay100"), slot("gain")}), chainWith(2, {slot("delay30")})};
    OfflineRenderOptions options;
    options.pluginHost = &h.host;
    const auto r = OfflineRenderer::render(p, {{1, impulseAt(500)}}, 0.1, options);
    REQUIRE(r);
    const int limiter = static_cast<int>(std::lround(1.5e-3 * kSr));
    CHECK(r->latencySamples == limiter + 100);
    CHECK(peakIndex(r->audio, 0) == 500 + r->latencySamples);
    CHECK(peakIndex(r->audio, 1) == 500 + r->latencySamples);
}

TEST_CASE("Plugins render: a missing plugin is bypassed and the project still renders") {
    Host h;
    Project p = twoChannels({directSource(1, {1})});
    p.sources[0].inserts = {slot("gone"), slot("delay16")};
    OfflineRenderOptions options;
    options.pluginHost = &h.host;
    const auto r = OfflineRenderer::render(p, {{1, impulseAt(1000)}}, 0.05, options);
    REQUIRE(r);
    CHECK(peakIndex(r->audio, 0) == 1000 + r->latencySamples);
    CHECK(r->audio.getSample(0, 1000 + r->latencySamples) == Approx(0.5f));
    CHECK(h.host.takeWarnings().size() == 1);

    // Without a plugin host at all every slot passes through.
    const auto bare = OfflineRenderer::render(p, {{1, impulseAt(1000)}}, 0.05);
    REQUIRE(bare);
    CHECK(peakIndex(bare->audio, 0) == 1000 + bare->latencySamples);
}

TEST_CASE("Plugins engine: parameter and bypass commands reach the plugin") {
    Project p = twoChannels({directSource(1, {1})});
    p.speakerChains = {chainWith(2, {slot("gain")})};
    p.sources.push_back(directSource(2, {2}));
    p.sources[0].inserts = {slot("gain")};
    Rig rig(p);
    rig.run(4800);
    CHECK(rig.last(0) == Approx(0.25f));

    rig.proc.push(cmd::SetInsertParam{InsertOwner::Source, 1, 0, 0, 0.25f});  // x0.5
    rig.proc.push(cmd::SetInsertParam{InsertOwner::Speaker, 2, 0, 0, 1.0f});  // x2
    rig.run(4800);
    CHECK(rig.last(0) == Approx(0.125f));
    CHECK(rig.last(1) == Approx(0.5f));

    rig.proc.push(cmd::SetInsertBypass{InsertOwner::Source, 1, 0, true});
    rig.run(4800);
    CHECK(rig.last(0) == Approx(0.25f));
    // Unknown targets are ignored.
    rig.proc.push(cmd::SetInsertParam{InsertOwner::Source, 99, 0, 0, 0.0f});
    rig.proc.push(cmd::SetInsertParam{InsertOwner::Source, 1, 3, 0, 0.0f});
    rig.proc.push(cmd::SetInsertBypass{InsertOwner::Speaker, 1, 0, true});
    rig.run(1280);
    CHECK(rig.last(0) == Approx(0.25f));
}

TEST_CASE("Plugins engine: a graph swap keeps plugin instances and their state") {
    Project p = twoChannels({directSource(1, {1})});
    p.sources[0].inserts = {slot("delay64")};
    p.speakerChains = {chainWith(1, {slot("gain")})};
    Rig rig(p);
    rig.run(2400);
    rig.plugin(InsertOwner::Speaker, 1, 0).memo = "running";
    const int created = TestPlugin::createdInstances.load();

    rig.project.sources.push_back(directSource(2, {2}));
    rig.proc.swapGraph(rig.build());
    rig.run(4800);
    CHECK(TestPlugin::createdInstances.load() == created);
    CHECK(rig.plugin(InsertOwner::Speaker, 1, 0).memo == "running");
    CHECK(rig.last(0) == Approx(0.25f));
    // Source 2 (no plugin) is padded to source 1's 64 samples; nothing jumped.
    CHECK(rig.proc.currentGraph()->sourceInsertLatency() == 64);
    CHECK(rig.maxStep(0, 4800) < 1e-6f);
}

TEST_CASE("Plugins recall: insert parameters ramp over the transition") {
    Project p = twoChannels({directSource(1, {1})});
    p.sources[0].inserts = {slot("gain", {{0, 0.5f}})};
    Rig rig(p);
    rig.run(4800);
    REQUIRE(rig.last(0) == Approx(0.25f));
    const int L = rig.proc.latencySamples();

    // Scenes are captured together with the live plugin state ("Update scene").
    rig.plugins.host.captureState(rig.project);
    Scene target = captureScene(rig.project, "t");
    target.sources[0].inserts[0].params[0] = 0.25f;  // x1 -> x0.5
    const int t0 = rig.pos();
    rig.recall(target, 1.0f);
    rig.run(48000 + 4800);
    // Stepped once per block from the live value; halfway it is near 0.75.
    CHECK(rig.at(0, t0 + L + 24000) == Approx(0.25f * 0.75f).margin(0.002));
    CHECK(rig.last(0) == Approx(0.125f));
    CHECK(rig.plugin(InsertOwner::Source, 1, 0).gainParameter().getValue() == Approx(0.25f));
}

TEST_CASE("Plugins recall: bypass in a scene switches with a fade") {
    Project p = twoChannels({directSource(1, {1})});
    p.speakerChains = {chainWith(1, {slot("gain", {{0, 0.0f}})})};  // plugin mutes
    Rig rig(p);
    rig.run(4800);
    REQUIRE(rig.last(0) == Approx(0.0f));
    rig.plugins.host.captureState(rig.project);
    Scene target = captureScene(rig.project, "t");
    target.speakerChains[0].inserts[0].bypassed = true;
    const int t0 = rig.pos();
    rig.recall(target, 0.0f);
    rig.run(4800);
    CHECK(rig.last(0) == Approx(0.25f));
    CHECK(rig.maxStep(0, t0) < 0.25f / 100.0f);  // 20 ms fade, no click
}

TEST_CASE("Plugins recall: a different plugin crossfades without dropping the source") {
    Project p = twoChannels({directSource(1, {1}), directSource(2, {2})});
    p.speakerChains = {chainWith(2, {slot("gain", {{0, 0.5f}})})};
    p.sources[0].inserts = {slot("gain", {{0, 0.5f}})};
    Rig rig(p);
    rig.run(4800);
    const int created = TestPlugin::createdInstances.load();

    rig.plugins.host.captureState(rig.project);
    Scene target = captureScene(rig.project, "t");
    target.sources[0].inserts = {slot("delay32", {{0, 0.25f}})};           // other plugin: x0.5, 32 samples
    target.speakerChains[0].inserts.push_back(slot("gain", {{0, 1.0f}}));  // slot added: x2
    const SceneDiff d = diffScenes(captureScene(rig.project, "c"), target, rig.project.layout);
    CHECK(d.sourceInsertChanges == std::vector<int>{1});
    CHECK(d.speakerInsertChanges == std::vector<int>{2});
    CHECK(d.replacedSources.empty());

    const int t0 = rig.pos();
    rig.recall(target, 2.0f);
    rig.run(9600);
    // The unchanged speaker slot kept its instance; two new ones were loaded.
    CHECK(TestPlugin::createdInstances.load() - created == 2);
    CHECK(rig.last(0) == Approx(0.125f));
    CHECK(rig.last(1) == Approx(0.5f));
    // 50 ms crossfades, no clicks. (The new delay32 instance starts with 32
    // samples of silence, which shows as a small dip early in the fade.)
    CHECK(rig.maxStep(0, t0) < 0.25f / 50.0f);
    CHECK(rig.maxStep(1, t0) < 0.25f / 1000.0f);
}

TEST_CASE("Plugins recall: plugin state stored in a scene is restored") {
    Project p = twoChannels({directSource(1, {1})});
    p.sources[0].inserts = {slot("gain")};
    Rig rig(p);
    rig.run(1280);

    // Set up a sound, store it as a scene (capture = what "Update scene" does).
    rig.plugin(InsertOwner::Source, 1, 0).gainParameter().setValue(0.1f);
    rig.plugin(InsertOwner::Source, 1, 0).memo = "scene A";
    rig.plugins.host.captureState(rig.project);
    const Scene a = captureScene(rig.project, "A");
    REQUIRE_FALSE(a.sources[0].inserts[0].stateBase64.empty());

    // Through the project file's JSON, as a scene would be stored.
    const Scene stored = Json(a).get<Scene>();
    REQUIRE(stored == a);

    // Tweak live, then recall the scene: the state comes back.
    rig.plugin(InsertOwner::Source, 1, 0).gainParameter().setValue(0.9f);
    rig.plugin(InsertOwner::Source, 1, 0).memo = "tweaked";
    rig.run(1280);
    rig.recall(stored, 0.0f);
    rig.run(4800);
    CHECK(rig.plugin(InsertOwner::Source, 1, 0).memo == "scene A");
    CHECK(rig.plugin(InsertOwner::Source, 1, 0).gainParameter().getValue() == Approx(0.1f));
    CHECK(rig.last(0) == Approx(0.25f * 0.2f));
}

TEST_CASE("Plugins engine: processing and recall with plugins do not allocate on the audio thread") {
    allocguard::setMode(allocguard::Mode::Count);
    Project p = twoChannels({directSource(1, {1}), directSource(2, {2})});
    p.sources[0].inserts = {slot("delay32"), slot("gain")};
    p.speakerChains = {chainWith(2, {slot("delay8")})};
    Rig rig(p);
    rig.run(1280);
    rig.plugins.host.captureState(rig.project);
    Scene target = captureScene(rig.project, "t");
    target.sources[0].inserts[1].params[0] = 0.2f;
    target.sources[0].inserts[0].bypassed = true;
    target.speakerChains[0].inserts = {slot("gain")};
    allocguard::resetViolations();
    rig.recall(target, 0.5f);
    rig.proc.push(cmd::SetInsertParam{InsertOwner::Speaker, 2, 0, 0, 0.3f});
    rig.run(48000);
    CHECK(allocguard::violations() == 0);
}
