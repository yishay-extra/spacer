// The plug-in UI's logic against a real PluginHost (WP11e): editor windows
// follow the host's instances across graph swaps and recalls, and "Update
// scene" stores what was changed in a plug-in editor.

#include <catch2/catch_test_macros.hpp>

#include "EngineTestUtils.h"
#include "TestPlugins.h"
#include "core/SceneDiff.h"
#include "engine/GraphBuilder.h"
#include "engine/PluginHost.h"
#include "ui/logic/PluginWindowBook.h"
#include "ui/logic/ProjectHistory.h"
#include "ui/logic/SceneCapture.h"
#include "ui/logic/SceneEditing.h"
#include "ui/logic/SpeakerChainEditing.h"

using namespace spacer;
using namespace spacer::testing;
using namespace spacer::pluginwindows;

namespace {

constexpr double kSr = 48000.0;
constexpr int kBlock = 128;

struct TempDir {
    juce::File dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                         .getNonexistentChildFile("spacer-plugin-ui-test", "", false);
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

    InstanceLookup lookup() {
        return [this](const SlotKey& k) {
            return reinterpret_cast<InstanceToken>(host.instanceFor(k.owner, k.ownerId, k.slot).get());
        };
    }
};

PluginSlot slot(const juce::String& kind) {
    PluginSlot s;
    s.pluginId = TestPluginFormat::id(kind).toStdString();
    return s;
}

Project makeUiProject() {
    SourceSpec src = makeSource(1, {}, SpatializerKind::Direct);
    src.directSpeakers = {1};
    src.inserts = {slot("gain")};
    Project p =
        testing::makeProject({makeSpeaker(1, -90.0f, 2.0f, 0), makeSpeaker(2, 90.0f, 2.0f, 1)}, {src});
    SpeakerChainSpec c;
    c.speakerId = 1;
    c.inserts = {slot("gain")};
    p.speakerChains = {c};
    return p;
}

/// Builds the chains of every slot the way GraphBuilder does for a graph swap.
std::vector<std::unique_ptr<InsertChain>> build(Host& h, const Project& p) {
    std::vector<std::unique_ptr<InsertChain>> chains;
    for (const auto& s : p.sources)
        chains.push_back(h.host.buildChain(InsertOwner::Source, s.id, s.inserts, 1, kSr, kBlock));
    for (const auto& c : p.speakerChains)
        chains.push_back(h.host.buildChain(InsertOwner::Speaker, c.speakerId, c.inserts, 1, kSr, kBlock));
    return chains;
}

}  // namespace

TEST_CASE("Plugin editor windows: source and speaker slots resolve to their live instances") {
    Host h;
    const Project p = makeUiProject();
    auto chains = build(h, p);
    WindowBook book;
    const SlotKey source{InsertOwner::Source, 1, 0};
    const SlotKey speaker{InsertOwner::Speaker, 1, 0};
    CHECK(book.open(p, source, h.lookup()) == WindowBook::OpenResult::Opened);
    CHECK(book.open(p, speaker, h.lookup()) == WindowBook::OpenResult::Opened);
    CHECK(book.find(source)->instance != book.find(speaker)->instance);
    CHECK(book.find(source)->instance ==
          reinterpret_cast<InstanceToken>(h.host.instanceFor(InsertOwner::Source, 1, 0).get()));
}

TEST_CASE("Plugin editor windows: an unchanged graph swap keeps the window on the same instance") {
    Host h;
    const Project p = makeUiProject();
    auto chains = build(h, p);
    WindowBook book;
    book.open(p, {InsertOwner::Source, 1, 0}, h.lookup());

    auto swapped = build(h, p);  // e.g. a source was added elsewhere: slots reused
    chains.clear();
    CHECK(book.reconcile(p, h.lookup()).empty());
}

TEST_CASE("Plugin editor windows: a recall that replaces the instance moves the window to it") {
    Host h;
    Project p = makeUiProject();
    auto chains = build(h, p);
    WindowBook book;
    const SlotKey key{InsertOwner::Source, 1, 0};
    REQUIRE(book.open(p, key, h.lookup()) == WindowBook::OpenResult::Opened);
    const InstanceToken before = book.find(key)->instance;
    auto shown = h.host.instanceFor(InsertOwner::Source, 1, 0);  // what the window holds

    // The scene stores another state for that slot: the graph build loads a new instance.
    p.sources[0].inserts[0].stateBase64 = "AAECAw==";
    auto next = build(h, p);
    auto plan = book.reconcile(p, h.lookup());
    CHECK(plan.close.empty());
    REQUIRE(plan.retarget.size() == 1);
    CHECK(plan.retarget[0].instance != before);
    CHECK(plan.retarget[0].instance ==
          reinterpret_cast<InstanceToken>(h.host.instanceFor(InsertOwner::Source, 1, 0).get()));
}

TEST_CASE("Plugin editor windows: removing the slot or the speaker closes the window") {
    Host h;
    Project p = makeUiProject();
    auto chains = build(h, p);
    WindowBook book;
    book.open(p, {InsertOwner::Source, 1, 0}, h.lookup());
    book.open(p, {InsertOwner::Speaker, 1, 0}, h.lookup());

    ProjectHistory history;
    history.perform(p, *chainedit::editRemoveInsert(p, 1, 0));
    p.sources[0].inserts.clear();
    chains =
        build(h, p);  // the old instances are released by the graph, but the registry may still know them
    const auto plan = book.reconcile(p, h.lookup());
    CHECK(plan.close.size() == 2);
    CHECK(book.entries().empty());
}

TEST_CASE("Plugin editor windows: a plug-in that fails to load has no editor to open") {
    Host h;
    Project p = makeUiProject();
    p.sources[0].inserts = {slot("fail")};
    auto chains = build(h, p);
    WindowBook book;
    CHECK(book.open(p, {InsertOwner::Source, 1, 0}, h.lookup()) == WindowBook::OpenResult::NotLoaded);
}

TEST_CASE("Update scene stores what was changed in a plug-in editor") {
    Host h;
    Project p = makeUiProject();
    auto chains = build(h, p);
    sceneedit::captureNew(p, "A");
    REQUIRE(p.scenes[0].sources[0].inserts[0].stateBase64.empty());

    // The user turns a knob / types in the plug-in's own editor.
    auto& plugin = dynamic_cast<TestPlugin&>(h.host.instanceFor(InsertOwner::Source, 1, 0)->instance());
    plugin.gainParameter().setValue(0.1f);
    plugin.memo = "from the editor";
    auto& speakerPlugin =
        dynamic_cast<TestPlugin&>(h.host.instanceFor(InsertOwner::Speaker, 1, 0)->instance());
    speakerPlugin.memo = "speaker editor";
    REQUIRE(p.sources[0].inserts[0].stateBase64.empty());  // the model does not know yet

    ProjectHistory history;
    history.perform(
        p, scenecapture::updateScene(p, "A", [&](Project& live) { return h.host.captureState(live); }));

    // The stored scene carries the editor state, for sources and speaker chains alike.
    const Scene& scene = p.scenes[0];
    CHECK_FALSE(scene.sources[0].inserts[0].stateBase64.empty());
    CHECK_FALSE(scene.speakerChains[0].inserts[0].stateBase64.empty());
    CHECK(scene.sources[0].inserts[0].stateBase64 == p.sources[0].inserts[0].stateBase64);

    // And recalling it later restores the editor state.
    plugin.memo = "changed again";
    Project other = p;
    h.host.captureState(other);  // a recall captures the live state first (CueEngine does)
    CHECK(other.sources[0].inserts[0].stateBase64 != scene.sources[0].inserts[0].stateBase64);
    applyScene(other, scene);
    auto restored = build(h, other);
    auto& after = dynamic_cast<TestPlugin&>(h.host.instanceFor(InsertOwner::Source, 1, 0)->instance());
    CHECK(after.memo == "from the editor");
}
