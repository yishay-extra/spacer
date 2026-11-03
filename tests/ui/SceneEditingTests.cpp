#include <catch2/catch_test_macros.hpp>

#include "core/LayoutPresets.h"
#include "core/SceneDiff.h"
#include "ui/logic/CueListEditing.h"
#include "ui/logic/CueListView.h"
#include "ui/logic/ProjectHistory.h"
#include "ui/logic/SceneEditing.h"

using namespace spacer;
namespace se = spacer::sceneedit;

namespace {
Project makeProject() {
    Project p;
    p.layout = *layoutpresets::load("quad");
    SourceSpec s;
    s.id = 1;
    s.name = "Voice";
    p.sources = {s};
    p.scenes.push_back(captureScene(p, "A"));
    p.sources[0].gainDb = -6.0f;
    p.scenes.push_back(captureScene(p, "B"));
    CueList l;
    l.name = "Show";
    for (const char* sc : {"A", "B", "A"}) {
        Cue c;
        c.name = std::string("cue ") + sc;
        c.sceneName = sc;
        l.cues.push_back(c);
    }
    p.cueLists.push_back(l);
    return p;
}
}  // namespace

TEST_CASE("SceneEditing: capture, update, rename, delete", "[ui]") {
    auto p = makeProject();
    p.sources[0].gainDb = -12.0f;

    CHECK(se::uniqueSceneName(p, "Scene") == "Scene");
    CHECK(se::uniqueSceneName(p, "A") == "A 2");
    CHECK(se::captureNew(p, " C "));
    CHECK(p.scenes.back().name == "C");
    CHECK(p.scenes.back().sources[0].gainDb == -12.0f);
    CHECK_FALSE(se::captureNew(p, "C"));
    CHECK_FALSE(se::captureNew(p, ""));

    CHECK(se::update(p, "A"));
    CHECK(p.scenes[0].sources[0].gainDb == -12.0f);
    CHECK(p.scenes[0].name == "A");
    CHECK_FALSE(se::update(p, "Nope"));

    CHECK(se::cuesUsing(p, "A").size() == 2);
    CHECK(se::rename(p, "A", "Intro"));
    CHECK(p.scenes[0].name == "Intro");
    CHECK(p.cueLists[0].cues[0].sceneName == "Intro");
    CHECK(p.cueLists[0].cues[1].sceneName == "B");
    CHECK(p.cueLists[0].cues[2].sceneName == "Intro");
    CHECK(se::rename(p, "Intro", "Intro"));
    CHECK_FALSE(se::rename(p, "Intro", "B"));
    CHECK_FALSE(se::rename(p, "Intro", " "));
    CHECK_FALSE(se::rename(p, "Nope", "x"));

    CHECK(se::remove(p, "B"));
    CHECK_FALSE(se::remove(p, "B"));
    CHECK(se::cuesUsing(p, "B").size() == 1);  // cues stay, dangling
}

TEST_CASE("SceneEditing: live status flags modified scenes", "[ui]") {
    auto p = makeProject();
    CHECK_FALSE(se::liveStatus(p, "Nope").known);
    // The live state is scene B's.
    CHECK(se::liveStatus(p, "B").known);
    CHECK_FALSE(se::liveStatus(p, "B").modified);
    CHECK(se::liveStatus(p, "A").modified);  // differs from A
    p.sources[0].gainDb = -7.0f;
    CHECK(se::liveStatus(p, "B").modified);
    se::update(p, "B");  // "Update scene" clears the marker
    CHECK_FALSE(se::liveStatus(p, "B").modified);
    // Layout and the other scenes do not count.
    p.layout.speakers[0].trimDb = 3.0f;
    CHECK_FALSE(se::liveStatus(p, "B").modified);
}

TEST_CASE("SceneEditing: show edits undo and redo only scenes and cue lists", "[ui]") {
    auto p = makeProject();
    ProjectHistory history;

    auto edit = edits::editShow(p, [](Project& q) { return se::rename(q, "A", "Intro"); }, "Rename Scene");
    REQUIRE(edits::isValid(edit));
    CHECK(edit.effect.kind == EngineEffect::Kind::None);
    REQUIRE(history.perform(p, std::move(edit)));
    CHECK(p.scenes[0].name == "Intro");
    CHECK(p.cueLists[0].cues[0].sceneName == "Intro");

    // A recall changes the live state outside the history; undo must not revert it.
    p.sources[0].gainDb = -20.0f;
    REQUIRE(history.undo(p));
    CHECK(p.scenes[0].name == "A");
    CHECK(p.cueLists[0].cues[0].sceneName == "A");
    CHECK(p.sources[0].gainDb == -20.0f);
    REQUIRE(history.redo(p));
    CHECK(p.scenes[0].name == "Intro");
    CHECK(history.undoName() == "Rename Scene");
}

TEST_CASE("SceneEditing: show edits that do nothing are invalid", "[ui]") {
    const auto p = makeProject();
    CHECK_FALSE(edits::isValid(edits::editShow(p, [](Project&) { return false; }, "x")));
    CHECK_FALSE(edits::isValid(edits::editShow(p, [](Project&) { return true; }, "x")));
    CHECK_FALSE(edits::isValid(edits::editShow(p, [](Project& q) { return se::rename(q, "A", "B"); }, "x")));
    CHECK(edits::isValid(edits::editShow(
        p, [](Project& q) { return cueedit::editCueField(q.cueLists[0], 0, cueedit::CueField::Name, "z"); },
        "x")));
}

TEST_CASE("CueListView: current / next highlighting and texts", "[ui]") {
    auto p = makeProject();
    p.cueLists[0].cues[1].trigger = Trigger{trigger::AfterPrevious{Period::seconds(4), 0.0}};
    const auto& list = p.cueLists[0];

    auto rows = cueview::rows(p, list, -1, 0);
    REQUIRE(rows.size() == 3);
    CHECK(cueview::highlight(rows[0]) == cueview::Highlight::Next);
    CHECK(cueview::highlight(rows[1]) == cueview::Highlight::None);
    CHECK(cueview::currentText(list, -1) == "Not started");
    CHECK(cueview::nextText(list, 0) == "Next: cue A (manual)");

    rows = cueview::rows(p, list, 0, 1);
    CHECK(cueview::highlight(rows[0]) == cueview::Highlight::Current);
    CHECK(cueview::highlight(rows[1]) == cueview::Highlight::Next);
    CHECK(cueview::currentText(list, 0) == "Cue 1 of 3: cue A");
    CHECK(cueview::nextText(list, 1) == "Next: cue B (after 4 s)");

    // A looping one-cue list: the cue is both current and next.
    CueList one;
    one.cues = {list.cues[0]};
    one.loop = true;
    CHECK(cueview::highlight(cueview::rows(p, one, 0, 0)[0]) == cueview::Highlight::CurrentAndNext);

    rows = cueview::rows(p, list, 2, -1);
    CHECK(cueview::nextText(list, -1) == "End of list");
    CHECK(cueview::currentText(CueList{}, -1) == "Empty list");

    se::remove(p, "B");
    CHECK(cueview::rows(p, list, -1, 0)[1].sceneMissing);
    CHECK_FALSE(cueview::rows(p, list, -1, 0)[0].sceneMissing);
}
