#include <catch2/catch_test_macros.hpp>

#include "core/LayoutPresets.h"
#include "core/SceneDiff.h"
#include "ui/logic/ProjectHistory.h"
#include "ui/logic/SceneCapture.h"
#include "ui/logic/SceneEditing.h"

using namespace spacer;

namespace {

Project makeProject() {
    Project p;
    p.layout = *layoutpresets::load("quad");
    SourceSpec s;
    s.id = 1;
    s.inserts = {PluginSlot{"SpacerTest:gain", "OLD", {}, false}};
    p.sources.push_back(s);
    sceneedit::captureNew(p, "A");
    return p;
}

/// Stands in for PluginHost::captureState: the "editor" changed the state.
scenecapture::CaptureLive editorChanged(std::vector<std::string>* log, const char* what = "EDITED") {
    return [log, what](Project& p) {
        log->push_back("capture");
        p.sources[0].inserts[0].stateBase64 = what;
        return 1;
    };
}

}  // namespace

TEST_CASE("Update scene captures plug-in state before it stores the live state", "[ui][plugins]") {
    Project p = makeProject();
    std::vector<std::string> log;
    ProjectHistory h;

    auto edit = scenecapture::updateScene(p, "A", editorChanged(&log));
    REQUIRE(edits::isValid(edit));
    CHECK(log == std::vector<std::string>{"capture"});
    // The model got the captured state, and so did the scene.
    CHECK(p.sources[0].inserts[0].stateBase64 == "EDITED");
    h.perform(p, std::move(edit));
    CHECK(p.scenes[0].sources[0].inserts[0].stateBase64 == "EDITED");

    // Undo restores the scene only; the live state keeps what the editor made.
    REQUIRE(h.undo(p));
    CHECK(p.scenes[0].sources[0].inserts[0].stateBase64 == "OLD");
    CHECK(p.sources[0].inserts[0].stateBase64 == "EDITED");
}

TEST_CASE("Without the capture the scene would keep the stale state", "[ui][plugins]") {
    // The failure the ordering prevents: updating from the model as it is.
    Project p = makeProject();
    auto edit = scenecapture::updateScene(p, "A", {});  // no capture hook
    CHECK_FALSE(edits::isValid(edit));                  // nothing differs: the scene is stale
    CHECK(p.scenes[0].sources[0].inserts[0].stateBase64 == "OLD");
}

TEST_CASE("New scene captures plug-in state too", "[ui][plugins]") {
    Project p = makeProject();
    std::vector<std::string> log;
    ProjectHistory h;
    auto edit = scenecapture::newScene(p, "B", editorChanged(&log));
    REQUIRE(edits::isValid(edit));
    h.perform(p, std::move(edit));
    REQUIRE(p.scenes.size() == 2);
    CHECK(p.scenes[1].name == "B");
    CHECK(p.scenes[1].sources[0].inserts[0].stateBase64 == "EDITED");
    CHECK(p.scenes[0].sources[0].inserts[0].stateBase64 == "OLD");  // others untouched
}

TEST_CASE("Update scene: an unknown scene or a taken name is refused, the capture still counts",
          "[ui][plugins]") {
    Project p = makeProject();
    std::vector<std::string> log;
    CHECK_FALSE(edits::isValid(scenecapture::updateScene(p, "missing", editorChanged(&log))));
    CHECK_FALSE(edits::isValid(scenecapture::newScene(p, "A", editorChanged(&log, "AGAIN"))));
    CHECK_FALSE(edits::isValid(scenecapture::newScene(p, "", editorChanged(&log, "AGAIN"))));
    CHECK(log.size() == 3);
    CHECK(p.sources[0].inserts[0].stateBase64 == "AGAIN");  // live model follows the plug-in regardless
}
