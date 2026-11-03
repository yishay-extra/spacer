#include <catch2/catch_test_macros.hpp>

#include "ui/logic/CueListEditing.h"

using namespace spacer;
namespace ce = spacer::cueedit;

namespace {
Project makeProject() {
    Project p;
    for (const char* n : {"A", "B", "C"}) {
        Scene s;
        s.name = n;
        p.scenes.push_back(s);
    }
    CueList l;
    l.name = "Show";
    for (const char* n : {"one", "two", "three"}) {
        Cue c;
        c.name = n;
        c.sceneName = "A";
        l.cues.push_back(c);
    }
    p.cueLists.push_back(l);
    return p;
}
std::vector<std::string> names(const CueList& l) {
    std::vector<std::string> out;
    for (const auto& c : l.cues) out.push_back(c.name);
    return out;
}
}  // namespace

TEST_CASE("CueListEditing: add, remove, duplicate and move cues", "[ui]") {
    auto p = makeProject();
    auto& l = p.cueLists[0];

    const Cue fresh = ce::makeCue(p, l);
    CHECK(fresh.name == "Cue 4");
    CHECK(fresh.sceneName == "A");
    CHECK(ce::makeCue(p, l, "C").sceneName == "C");

    CHECK(ce::addCue(l, fresh, 0) == 1);  // after the first
    CHECK(names(l) == std::vector<std::string>{"one", "Cue 4", "two", "three"});
    CHECK(ce::addCue(l, ce::makeCue(p, l), -1) == 0);
    const int sizeBefore = static_cast<int>(l.cues.size());  // operand order of == is unspecified
    CHECK(ce::addCue(l, ce::makeCue(p, l), 99) == sizeBefore);

    CHECK(ce::removeCue(l, 0));
    CHECK_FALSE(ce::removeCue(l, 99));
    CHECK_FALSE(ce::removeCue(l, -1));

    l = makeProject().cueLists[0];
    CHECK(ce::duplicateCue(l, 1) == 2);
    CHECK(names(l) == std::vector<std::string>{"one", "two", "two copy", "three"});
    CHECK_FALSE(ce::duplicateCue(l, 9));

    CHECK(ce::moveCue(l, 0, 3));
    CHECK(names(l) == std::vector<std::string>{"two", "two copy", "three", "one"});
    CHECK(ce::moveCue(l, 3, 0));
    CHECK(names(l) == std::vector<std::string>{"one", "two", "two copy", "three"});
    CHECK_FALSE(ce::moveCue(l, 1, 1));
    CHECK_FALSE(ce::moveCue(l, 1, 4));
}

TEST_CASE("CueListEditing: cue count is bounded", "[ui]") {
    auto p = makeProject();
    auto& l = p.cueLists[0];
    while (l.cues.size() < ce::kMaxCuesPerList) REQUIRE(ce::addCue(l, ce::makeCue(p, l), 0));
    CHECK_FALSE(ce::addCue(l, ce::makeCue(p, l), 0));
    CHECK_FALSE(ce::duplicateCue(l, 0));
}

TEST_CASE("CueListEditing: field text round trip and validation", "[ui]") {
    Cue c;
    c.name = "Intro";
    c.sceneName = "A";
    using F = ce::CueField;

    CHECK(ce::setField(c, F::Name, "  Verse "));
    CHECK(c.name == "Verse");
    CHECK_FALSE(ce::setField(c, F::Name, "   "));
    CHECK(c.name == "Verse");

    CHECK(ce::setField(c, F::Transition, "2.5 s"));
    CHECK(c.transitionSeconds == 2.5f);
    CHECK(ce::formatField(c, F::Transition) == "2.5");
    for (const char* bad : {"", "x", "-1", "3601", "nan", "inf", "1 2"})
        CHECK_FALSE(ce::setField(c, F::Transition, bad));
    CHECK(c.transitionSeconds == 2.5f);

    CHECK(ce::setField(c, F::PostWait, "10"));
    CHECK(ce::formatField(c, F::PostWait) == "10");
    CHECK_FALSE(ce::setField(c, F::PostWait, "-0.1"));

    CHECK(ce::setField(c, F::RestartPhase, "yes"));
    CHECK(c.restartMotionPhase);
    CHECK(ce::formatField(c, F::RestartPhase) == "yes");
    CHECK(ce::setField(c, F::RestartPhase, "off"));
    CHECK_FALSE(c.restartMotionPhase);
    CHECK_FALSE(ce::setField(c, F::RestartPhase, "maybe"));

    CHECK(ce::setField(c, F::Trigger, "after 4 s +2"));
    CHECK(ce::formatField(c, F::Trigger) == "after 4 s +2");
    CHECK_FALSE(ce::setField(c, F::Trigger, "whenever"));
    CHECK(ce::formatField(c, F::Trigger) == "after 4 s +2");

    CHECK(ce::setField(c, F::Scene, "B"));
    CHECK(c.sceneName == "B");
    CHECK_FALSE(ce::setField(c, F::Scene, ""));
}

TEST_CASE("CueListEditing: editCueField reports only real changes", "[ui]") {
    auto p = makeProject();
    auto& l = p.cueLists[0];
    CHECK_FALSE(ce::editCueField(l, 0, ce::CueField::Name, "one"));  // unchanged
    CHECK_FALSE(ce::editCueField(l, 0, ce::CueField::Name, ""));     // invalid
    CHECK_FALSE(ce::editCueField(l, 7, ce::CueField::Name, "x"));    // out of range
    CHECK(ce::editCueField(l, 0, ce::CueField::Name, "uno"));
    CHECK(l.cues[0].name == "uno");
}

TEST_CASE("CueListEditing: manage cue lists", "[ui]") {
    auto p = makeProject();
    CHECK(ce::indexOfCueList(p, "Show") == 0);
    CHECK(ce::indexOfCueList(p, "Nope") == -1);
    CHECK(ce::uniqueCueListName(p, "Show") == "Show 2");
    CHECK(ce::uniqueCueListName(p, "Other") == "Other");

    CHECK(ce::addCueList(p, " Encore ") == 1);
    CHECK(p.cueLists[1].name == "Encore");
    CHECK_FALSE(ce::addCueList(p, "Encore"));
    CHECK_FALSE(ce::addCueList(p, " "));

    CHECK(ce::renameCueList(p, 1, "Finale"));
    CHECK(ce::renameCueList(p, 1, "Finale"));  // same name: fine
    CHECK_FALSE(ce::renameCueList(p, 1, "Show"));
    CHECK_FALSE(ce::renameCueList(p, 1, ""));
    CHECK_FALSE(ce::renameCueList(p, 5, "x"));

    CHECK(ce::duplicateCueList(p, 0) == 2);
    CHECK(p.cueLists[2].name == "Show copy");
    CHECK(p.cueLists[2].cues == p.cueLists[0].cues);
    CHECK(ce::duplicateCueList(p, 0) == 3);
    CHECK(p.cueLists[3].name == "Show copy 2");

    CHECK(ce::removeCueList(p, 1));
    CHECK_FALSE(ce::removeCueList(p, 9));
    CHECK(p.cueLists.size() == 3);
}
