#include <catch2/catch_test_macros.hpp>

#include "core/LayoutPresets.h"
#include "core/Validate.h"
#include "ui/logic/EngineSync.h"
#include "ui/logic/LayoutEditing.h"
#include "ui/logic/ProjectHistory.h"
#include "ui/logic/SpeakerChainEditing.h"

using namespace spacer;
using namespace spacer::chainedit;
using Kind = EngineEffect::Kind;

namespace {

const std::string kPlugin = "AudioUnit:Effects/aufx,dely,appl";

Project makeProject() {
    Project p;
    p.layout = *layoutpresets::load("quad");
    p.layout.speakers[0].trimDb = -3.0f;
    p.layout.speakers[0].userDelayMs = 2.0f;
    p.layout.speakers[0].polarityInverted = true;
    return p;
}

int firstSpeaker(const Project& p) {
    return p.layout.speakers[0].id;
}

bool valid(const Project& p) {
    return !hasErrors(validate(p));
}

}  // namespace

TEST_CASE("Speaker inserts: the first insert creates a chain that keeps the speaker's values",
          "[ui][plugins]") {
    Project p = makeProject();
    const int id = firstSpeaker(p);
    REQUIRE(p.speakerChains.empty());

    const auto edit = editAddInsert(p, id, kPlugin);
    REQUIRE(edit);
    CHECK(edit->effect.kind == Kind::Structural);
    ProjectHistory h;
    h.perform(p, *edit);

    REQUIRE(p.speakerChains.size() == 1);
    const auto& c = p.speakerChains[0];
    CHECK(c.speakerId == id);
    REQUIRE(c.inserts.size() == 1);
    CHECK(c.inserts[0].pluginId == kPlugin);
    // The chain overrides the layout values (SPEC §7): it must not change them.
    CHECK(c.trimDb == -3.0f);
    CHECK(c.userDelayMs == 2.0f);
    CHECK(c.polarityInverted);
    CHECK(valid(p));

    // Undo removes the chain again.
    REQUIRE(h.undo(p));
    CHECK(p.speakerChains.empty());
    REQUIRE(h.redo(p));
    CHECK(p.speakerChains.size() == 1);
}

TEST_CASE("Speaker inserts: at most four, no empty id, only existing speakers", "[ui][plugins]") {
    Project p = makeProject();
    const int id = firstSpeaker(p);
    ProjectHistory h;
    for (int i = 0; i < kMaxInserts; ++i) {
        auto edit = editAddInsert(p, id, kPlugin);
        REQUIRE(edit);
        h.perform(p, *edit);
    }
    CHECK_FALSE(editAddInsert(p, id, kPlugin));  // SPEC §1: 4 per speaker
    CHECK_FALSE(editAddInsert(p, id, ""));
    CHECK_FALSE(editAddInsert(p, 9999, kPlugin));
    CHECK(valid(p));
    CHECK(insertRows(p, id).size() == 4);
    CHECK(insertRows(p, 9999).empty());
}

TEST_CASE("Speaker inserts: bypass is a live flag, remove rebuilds", "[ui][plugins]") {
    Project p = makeProject();
    const int id = firstSpeaker(p);
    ProjectHistory h;
    h.perform(p, *editAddInsert(p, id, kPlugin));
    h.perform(p, *editAddInsert(p, id, kPlugin));

    const auto bypass = editInsertBypass(p, id, 1, true);
    REQUIRE(bypass);
    CHECK(bypass->effect.kind == Kind::SpeakerInsertBypass);
    CHECK(bypass->effect.id == id);
    CHECK(bypass->effect.channel == 1);
    const auto effect = h.perform(p, *bypass);
    REQUIRE(effect);
    CHECK(p.speakerChains[0].inserts[1].bypassed);
    CHECK_FALSE(p.speakerChains[0].inserts[0].bypassed);

    // The engine gets a SetInsertBypass for the speaker, no graph rebuild.
    const auto plan = planEngineSync(p, *effect);
    CHECK_FALSE(plan.rebuildGraph);
    REQUIRE(plan.commands.size() == 1);
    const auto* cmd = std::get_if<cmd::SetInsertBypass>(&plan.commands[0]);
    REQUIRE(cmd);
    CHECK(cmd->owner == InsertOwner::Speaker);
    CHECK(cmd->id == id);
    CHECK(cmd->slot == 1);
    CHECK(cmd->bypassed);

    // Undo goes through the same path and sends the old value.
    const auto undone = h.undo(p);
    REQUIRE(undone);
    const auto* back = std::get_if<cmd::SetInsertBypass>(&planEngineSync(p, *undone).commands.at(0));
    REQUIRE(back);
    CHECK_FALSE(back->bypassed);

    CHECK_FALSE(editInsertBypass(p, id, 5, true));
    CHECK_FALSE(editInsertBypass(p, id, 0, false));  // nothing changes
    const auto remove = editRemoveInsert(p, id, 0);
    REQUIRE(remove);
    CHECK(remove->effect.kind == Kind::Structural);
}

TEST_CASE("Speaker inserts: removing the last one drops a chain that holds nothing else", "[ui][plugins]") {
    Project p = makeProject();
    const int id = firstSpeaker(p);
    ProjectHistory h;
    h.perform(p, *editAddInsert(p, id, kPlugin));
    h.perform(p, *editRemoveInsert(p, id, 0));
    // The layout's trim / delay / polarity apply again (a chain would override them).
    CHECK(p.speakerChains.empty());
    REQUIRE(h.undo(p));
    REQUIRE(p.speakerChains.size() == 1);
    CHECK(p.speakerChains[0].inserts.size() == 1);

    // A chain that carries its own settings stays.
    Project q = makeProject();
    h.clear();
    h.perform(q, *editAddInsert(q, id, kPlugin));
    q.speakerChains[0].mute = true;
    h.perform(q, *editRemoveInsert(q, id, 0));
    REQUIRE(q.speakerChains.size() == 1);
    CHECK(q.speakerChains[0].inserts.empty());
    CHECK(q.speakerChains[0].mute);
}

TEST_CASE("Speaker inserts: editing one speaker leaves the other chains alone", "[ui][plugins]") {
    Project p = makeProject();
    const int a = p.layout.speakers[0].id;
    const int b = p.layout.speakers[1].id;
    ProjectHistory h;
    h.perform(p, *editAddInsert(p, a, kPlugin));
    h.perform(p, *editAddInsert(p, b, "SpacerTest:gain"));
    REQUIRE(p.speakerChains.size() == 2);
    h.perform(p, *editRemoveInsert(p, a, 0));
    REQUIRE(p.speakerChains.size() == 1);
    CHECK(p.speakerChains[0].speakerId == b);
    CHECK(p.speakerChains[0].inserts[0].pluginId == "SpacerTest:gain");
}

TEST_CASE("Layout edits drop the chains of speakers that are gone, undo brings them back", "[ui][plugins]") {
    Project p = makeProject();
    const int id = firstSpeaker(p);
    ProjectHistory h;
    h.perform(p, *editAddInsert(p, id, kPlugin));
    REQUIRE(valid(p));

    Layout smaller = p.layout;
    REQUIRE(layoutedit::removeSpeaker(smaller, id));
    h.perform(p, edits::setLayout(p, smaller, "Remove Speaker"));
    CHECK(p.speakerChains.empty());
    CHECK(valid(p));  // a chain of an unknown speaker would be an error

    REQUIRE(h.undo(p));
    REQUIRE(p.speakerChains.size() == 1);
    CHECK(p.speakerChains[0].speakerId == id);
    CHECK(valid(p));

    // A layout edit that keeps the speaker keeps the chain.
    Layout moved = p.layout;
    moved.name = "renamed";
    h.perform(p, edits::setLayout(p, moved, "Rename Layout"));
    CHECK(p.speakerChains.size() == 1);
}
