#include <catch2/catch_test_macros.hpp>

#include "core/LayoutPresets.h"
#include "ui/logic/LayoutEditing.h"
#include "ui/logic/ProjectHistory.h"
#include "ui/logic/SourceGeometry.h"

using namespace spacer;
using Kind = EngineEffect::Kind;

namespace {

Project makeProject() {
    Project p;
    p.layout = *layoutpresets::load("quad");
    SourceSpec a;
    a.id = 1;
    a.name = "A";
    a.motion.value = ManualMotion{{0, 0, 2}, 20.0f};
    SourceSpec b = a;
    b.id = 2;
    b.name = "B";
    b.motion.value = OrbitMotion{};
    p.sources = {a, b};
    return p;
}

ProjectEdit moveSource(const Project& p, int id, Cartesian delta) {
    SourceSpec s;
    for (const auto& x : p.sources)
        if (x.id == id) s = x;
    auto e = edits::setSource(p, dragSource(s, 0, delta), "Move Source");
    e.mergeKey = "drag:" + std::to_string(id);
    return e;
}

}  // namespace

TEST_CASE("Undo and redo restore a layout edit", "[ui][undo]") {
    Project p = makeProject();
    const Project original = p;
    ProjectHistory h;
    Layout l = p.layout;
    REQUIRE(layoutedit::addSpeaker(l, {45, 0, 2}));
    const auto effect = h.perform(p, edits::setLayout(p, l, "Add Speaker"));
    REQUIRE(effect);
    REQUIRE(effect->kind == Kind::Structural);
    REQUIRE(p.layout.speakers.size() == 5);
    REQUIRE(h.canUndo());
    REQUIRE(h.undoName() == "Add Speaker");

    REQUIRE(h.undo(p));
    REQUIRE(p == original);
    REQUIRE(h.canRedo());
    REQUIRE(h.redoName() == "Add Speaker");
    REQUIRE(h.redo(p));
    REQUIRE(p.layout == l);
    REQUIRE_FALSE(h.redo(p));
}

TEST_CASE("A new edit clears the redo list", "[ui][undo]") {
    Project p = makeProject();
    ProjectHistory h;
    h.perform(p, moveSource(p, 1, {1, 0, 0}));
    h.undo(p);
    REQUIRE(h.canRedo());
    h.perform(p, moveSource(p, 1, {0, 1, 0}));
    REQUIRE_FALSE(h.canRedo());
}

TEST_CASE("One drag gesture is one undo step", "[ui][undo]") {
    Project p = makeProject();
    const Project original = p;
    ProjectHistory h;
    h.beginGesture();
    for (int i = 0; i < 10; ++i) h.perform(p, moveSource(p, 1, {0.1f, 0, 0}));
    h.endGesture();
    REQUIRE(h.undoDepth() == 1);
    const Project moved = p;
    REQUIRE_FALSE(moved == original);

    // A second gesture on the same source is a separate step.
    h.beginGesture();
    h.perform(p, moveSource(p, 1, {0, 0.5f, 0}));
    h.endGesture();
    REQUIRE(h.undoDepth() == 2);

    h.undo(p);
    REQUIRE(p == moved);
    h.undo(p);
    REQUIRE(p == original);
    h.redo(p);
    REQUIRE(p == moved);  // redo replays the whole first drag
}

TEST_CASE("Edits outside a gesture never merge", "[ui][undo]") {
    Project p = makeProject();
    ProjectHistory h;
    h.perform(p, moveSource(p, 1, {0.1f, 0, 0}));
    h.perform(p, moveSource(p, 1, {0.1f, 0, 0}));
    REQUIRE(h.undoDepth() == 2);
    // Different keys inside one gesture do not merge either.
    h.beginGesture();
    h.perform(p, moveSource(p, 1, {0.1f, 0, 0}));
    h.perform(p, moveSource(p, 2, {0.1f, 0, 0}));
    h.endGesture();
    REQUIRE(h.undoDepth() == 4);
}

TEST_CASE("Undoing a source edit restores only that source", "[ui][undo]") {
    Project p = makeProject();
    ProjectHistory h;
    h.perform(p, moveSource(p, 1, {1, 0, 0}));
    // Something that is not an edit (a scene recall) changes another source and the master.
    p.sources[1].gainDb = -12.0f;
    p.master.gainDb = -3.0f;
    h.undo(p);
    REQUIRE(p.sources[0] == makeProject().sources[0]);
    REQUIRE(p.sources[1].gainDb == -12.0f);
    REQUIRE(p.master.gainDb == -3.0f);
}

TEST_CASE("Edit effects distinguish parameter and structural changes", "[ui][undo]") {
    const Project p = makeProject();
    REQUIRE(moveSource(p, 1, {1, 0, 0}).effect.kind == Kind::Source);
    REQUIRE(moveSource(p, 1, {1, 0, 0}).effect.id == 1);
    REQUIRE(moveSource(p, 2, {1, 0, 0}).effect.kind == Kind::Structural);  // Orbit: rebuild
    SourceSpec s = p.sources[0];
    s.spatializer = SpatializerKind::Dbap;
    REQUIRE(edits::setSource(p, s).effect.kind == Kind::Structural);
    s = p.sources[0];
    s.gainDb = -6.0f;
    REQUIRE(edits::setSource(p, s).effect.kind == Kind::Source);
}

TEST_CASE("Invalid edits do nothing", "[ui][undo]") {
    Project p = makeProject();
    const Project original = p;
    ProjectHistory h;
    SourceSpec ghost;
    ghost.id = 99;
    REQUIRE_FALSE(h.perform(p, edits::setSource(p, ghost)));
    REQUIRE_FALSE(h.perform(p, ProjectEdit{}));
    REQUIRE(p == original);
    REQUIRE_FALSE(h.canUndo());
    REQUIRE_FALSE(h.undo(p));
}

TEST_CASE("History is bounded and can be cleared", "[ui][undo]") {
    Project p = makeProject();
    ProjectHistory h(5);
    for (int i = 0; i < 12; ++i) h.perform(p, moveSource(p, 1, {0.1f, 0, 0}));
    REQUIRE(h.undoDepth() == 5);
    h.clear();
    REQUIRE_FALSE(h.canUndo());
    REQUIRE_FALSE(h.canRedo());
}
