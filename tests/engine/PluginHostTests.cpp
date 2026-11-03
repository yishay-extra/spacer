#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <memory>

#include "EngineTestUtils.h"
#include "TestPlugins.h"
#include "engine/GraphBuilder.h"
#include "engine/PluginHost.h"
#include "engine/PluginState.h"

using Catch::Approx;
using namespace spacer;
using namespace spacer::testing;

namespace {

constexpr double kSr = 48000.0;
constexpr int kBlock = 128;

struct TempDir {
    juce::File dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                         .getNonexistentChildFile("spacer-plugin-test", "", false);
    TempDir() { dir.createDirectory(); }
    ~TempDir() { dir.deleteRecursively(); }
};

PluginHost::Options inProcess(const juce::File& dir) {
    PluginHost::Options o;
    o.cacheDirectory = dir;
    return o;
}

PluginSlot slot(const juce::String& kind, std::map<int, float> params = {}) {
    PluginSlot s;
    s.pluginId = TestPluginFormat::id(kind).toStdString();
    s.params = std::move(params);
    return s;
}

TestPlugin& testPlugin(const InsertChain& chain, int slot) {
    return dynamic_cast<TestPlugin&>(chain.plugin(slot)->instance());
}

}  // namespace

TEST_CASE("PluginState: standard base64 round trip") {
    juce::MemoryBlock data;
    const unsigned char bytes[] = {0, 1, 2, 3};
    data.append(bytes, 4);
    CHECK(pluginstate::encode(data) == "AAECAw==");
    const auto back = pluginstate::decode("AAECAw==");
    REQUIRE(back);
    CHECK(*back == data);
    CHECK(pluginstate::decode("")->isEmpty());
    CHECK_FALSE(pluginstate::decode("not base64 !!"));
}

TEST_CASE("PluginHost: resolves an unscanned plugin on demand and caches it") {
    TempDir tmp;
    {
        PluginHost host(inProcess(tmp.dir), addTestFormat);
        CHECK_FALSE(host.scansOutOfProcess());
        const auto d = host.resolve(TestPluginFormat::id("gain").toStdString());
        REQUIRE(d);
        CHECK(d->pluginFormatName == TestPluginFormat::kName);
        CHECK_FALSE(host.resolve("SpacerTest:nonexistent"));
        CHECK_FALSE(host.resolve("SomeOtherFormat:thing"));
    }
    PluginHost again(inProcess(tmp.dir), addTestFormat);
    CHECK(again.cache().hasIdentifier(TestPluginFormat::id("gain")));
}

TEST_CASE("PluginHost: missing or unloadable plugins leave the slot bypassed with a warning") {
    TempDir tmp;
    PluginHost host(inProcess(tmp.dir), addTestFormat);
    auto chain =
        host.buildChain(InsertOwner::Source, 3,
                        {slot("gain"), slot("nonexistent"), slot("fail"), PluginSlot{}}, 1, kSr, kBlock);
    REQUIRE(chain->numSlots() == 4);
    CHECK_FALSE(chain->isMissing(0));
    CHECK(chain->isMissing(1));
    CHECK(chain->isMissing(2));
    CHECK(chain->isMissing(3));
    const auto warnings = host.takeWarnings();
    REQUIRE(warnings.size() == 2);  // the empty pluginId is just an empty slot
    CHECK(warnings[0].find("source 3 insert 2") != std::string::npos);
    CHECK(warnings[0].find("not found") != std::string::npos);
    CHECK(warnings[1].find("failed to load") != std::string::npos);
    CHECK(host.takeWarnings().empty());
}

TEST_CASE("PluginHost: an unchanged slot reuses its instance; another state reloads") {
    TempDir tmp;
    PluginHost host(inProcess(tmp.dir), addTestFormat);
    const int created = TestPlugin::createdInstances.load();
    auto first = host.buildChain(InsertOwner::Speaker, 2, {slot("gain")}, 1, kSr, kBlock);
    auto second = host.buildChain(InsertOwner::Speaker, 2, {slot("gain")}, 1, kSr, kBlock);
    CHECK(TestPlugin::createdInstances.load() - created == 1);
    CHECK(first->sameInstancesAs(*second));

    // Same slot of another owner, another channel count or another rate: new instances.
    auto other = host.buildChain(InsertOwner::Speaker, 3, {slot("gain")}, 1, kSr, kBlock);
    auto stereo = host.buildChain(InsertOwner::Speaker, 2, {slot("gain")}, 2, kSr, kBlock);
    auto rate = host.buildChain(InsertOwner::Speaker, 2, {slot("gain")}, 1, 96000.0, kBlock);
    CHECK(TestPlugin::createdInstances.load() - created == 4);

    PluginSlot withState = slot("gain");
    withState.stateBase64 = pluginstate::capture(first->plugin(0)->instance());
    auto third = host.buildChain(InsertOwner::Speaker, 2, {withState}, 1, kSr, kBlock);
    CHECK_FALSE(third->sameInstancesAs(*first));

    // Instances die with the last chain that holds them.
    const int live = host.numLiveInstances();
    first.reset();
    second.reset();
    CHECK(host.numLiveInstances() == live - 1);
}

TEST_CASE("PluginHost: rebuilding keeps editor edits and applies model parameter changes") {
    TempDir tmp;
    PluginHost host(inProcess(tmp.dir), addTestFormat);
    auto chain = host.buildChain(InsertOwner::Source, 1, {slot("gain", {{0, 0.5f}})}, 1, kSr, kBlock);
    testPlugin(*chain, 0).gainParameter().setValue(0.9f);  // "editor" edit

    auto same = host.buildChain(InsertOwner::Source, 1, {slot("gain", {{0, 0.5f}})}, 1, kSr, kBlock);
    CHECK(testPlugin(*same, 0).gainParameter().getValue() == Approx(0.9f));

    auto changed = host.buildChain(InsertOwner::Source, 1, {slot("gain", {{0, 0.2f}})}, 1, kSr, kBlock);
    CHECK(changed->sameInstancesAs(*chain));
    CHECK(testPlugin(*changed, 0).gainParameter().getValue() == Approx(0.2f));
}

TEST_CASE("PluginHost: state round-trips through the model (capture, then load elsewhere)") {
    TempDir tmp;
    Project project = makeProject({makeSpeaker(1, 0.0f, 2.0f, 0)}, {makeSource(1, {0, 0, 2})});
    project.sources[0].inserts = {slot("delay16", {{0, 0.0f}})};
    SpeakerChainSpec chain;
    chain.speakerId = 1;
    chain.inserts = {slot("gain")};
    project.speakerChains = {chain};

    std::string sourceState;
    {
        PluginHost host(inProcess(tmp.dir), addTestFormat);
        auto c = host.buildChain(InsertOwner::Source, 1, project.sources[0].inserts, 1, kSr, kBlock);
        auto s = host.buildChain(InsertOwner::Speaker, 1, project.speakerChains[0].inserts, 1, kSr, kBlock);
        testPlugin(*c, 0).gainParameter().setValue(0.3f);
        testPlugin(*c, 0).memo = "hello";
        CHECK(host.captureState(project) == 2);
        sourceState = project.sources[0].inserts[0].stateBase64;
        CHECK_FALSE(sourceState.empty());
        CHECK(project.sources[0].inserts[0].params.at(0) == Approx(0.3f));  // listed params are read back
        CHECK(project.speakerChains[0].inserts[0].params.empty());

        // The captured state is now the instance's known state: a rebuild from it reuses.
        auto again = host.buildChain(InsertOwner::Source, 1, project.sources[0].inserts, 1, kSr, kBlock);
        CHECK(again->sameInstancesAs(*c));
    }

    // Through JSON and into a fresh host (another session).
    PluginHost fresh(inProcess(tmp.dir), addTestFormat);
    PluginSlot loaded = project.sources[0].inserts[0];
    loaded.params.clear();
    auto c = fresh.buildChain(InsertOwner::Source, 1, {loaded}, 1, kSr, kBlock);
    CHECK(testPlugin(*c, 0).gainParameter().getValue() == Approx(0.3f));
    CHECK(testPlugin(*c, 0).memo == "hello");

    // Params are applied on top of the state.
    loaded.params[0] = 0.7f;
    auto d = fresh.buildChain(InsertOwner::Source, 2, {loaded}, 1, kSr, kBlock);
    CHECK(testPlugin(*d, 0).gainParameter().getValue() == Approx(0.7f));
    CHECK(testPlugin(*d, 0).memo == "hello");
}

TEST_CASE("PluginHost: a latency change reported by a plugin is flagged once") {
    TempDir tmp;
    PluginHost host(inProcess(tmp.dir), addTestFormat);
    auto chain = host.buildChain(InsertOwner::Source, 1, {slot("delay8")}, 1, kSr, kBlock);
    CHECK(chain->latencySamples() == 8);
    CHECK_FALSE(host.takeLatencyChanged());
    testPlugin(*chain, 0).reportLatency(32);
    CHECK(host.takeLatencyChanged());
    CHECK_FALSE(host.takeLatencyChanged());
    // A rebuild picks the new latency up (the instance is re-prepared only when reloaded,
    // so the graph keeps aligning to the latency the chain was built with).
    CHECK(chain->latencySamples() == 8);
}

TEST_CASE("GraphBuilder: a project referencing a missing plugin still builds") {
    TempDir tmp;
    PluginHost host(inProcess(tmp.dir), addTestFormat);
    Project project = makeProject({makeSpeaker(1, -30.0f, 2.0f, 0), makeSpeaker(2, 30.0f, 2.0f, 1)},
                                  {makeSource(1, {0, 0, 2})});
    project.sources[0].inserts = {slot("gone"), slot("delay10")};
    SpeakerChainSpec chain;
    chain.speakerId = 2;
    chain.inserts = {PluginSlot{"AudioUnit:Effects/aufx,nope,none", "", {}, false}};
    project.speakerChains = {chain};
    GraphBuilder builder(project);
    builder.setPluginHost(&host);
    auto graph = builder.build(kSr, kBlock);
    REQUIRE(graph);
    CHECK(graph->sourceInsertLatency() == 10);
    CHECK(graph->speakerInsertLatency() == 0);
    CHECK(graph->findInserts(InsertOwner::Source, 1)->chain().isMissing(0));
    CHECK(graph->findInserts(InsertOwner::Speaker, 2)->chain().isMissing(0));
    CHECK(host.takeWarnings().size() == 2);
}

TEST_CASE("PluginHost: app defaults put the scanner next to the executable") {
    const auto o = PluginHost::Options::appDefaults();
    CHECK(o.scannerExecutable.getFileName() == "spacer-plugin-scanner");
    CHECK(o.scannerExecutable.getParentDirectory() ==
          juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory());
    CHECK(o.cacheDirectory.getFileName() == "Spacer");
}
