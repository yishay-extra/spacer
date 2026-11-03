#include <catch2/catch_test_macros.hpp>

#include "core/LayoutPresets.h"
#include "ui/logic/InsertEditing.h"

using namespace spacer;
using namespace spacer::insertedit;

TEST_CASE("Insert names: readable, with a fallback for empty and odd ids", "[ui][plugins]") {
    CHECK(fallbackName("AudioUnit:Effects/aufx,dely,appl") == "aufx,dely,appl");
    CHECK(fallbackName("SpacerTest:gain") == "gain");
    CHECK(fallbackName("plain") == "plain");
    CHECK(fallbackName("") == "(empty)");
    CHECK(fallbackName("ends/") == "ends/");  // nothing after the separator: keep the id
}

TEST_CASE("Insert rows carry the bypass flag per slot", "[ui][plugins]") {
    PluginSlot a;
    a.pluginId = "SpacerTest:gain";
    PluginSlot b;
    b.pluginId = "SpacerTest:delay64";
    b.bypassed = true;
    const auto r = rows({a, b, PluginSlot{}});
    REQUIRE(r.size() == 3);
    CHECK(r[0].name == "gain");
    CHECK_FALSE(r[0].bypassed);
    CHECK(r[1].bypassed);
    CHECK(r[2].name == "(empty)");
}

TEST_CASE("slotsOf finds source inserts and speaker-chain inserts", "[ui][plugins]") {
    Project p;
    p.layout = *layoutpresets::load("quad");
    SourceSpec s;
    s.id = 4;
    s.inserts = {PluginSlot{"x", "", {}, false}};
    p.sources.push_back(s);
    SpeakerChainSpec c;
    c.speakerId = p.layout.speakers[1].id;
    c.inserts = {PluginSlot{"y", "", {}, false}, PluginSlot{"z", "", {}, false}};
    p.speakerChains.push_back(c);

    REQUIRE(slotsOf(p, InsertOwner::Source, 4));
    CHECK(slotsOf(p, InsertOwner::Source, 4)->size() == 1);
    CHECK_FALSE(slotsOf(p, InsertOwner::Source, 5));
    REQUIRE(slotsOf(p, InsertOwner::Speaker, c.speakerId));
    CHECK(slotsOf(p, InsertOwner::Speaker, c.speakerId)->size() == 2);
    // A speaker without a chain has no inserts.
    CHECK_FALSE(slotsOf(p, InsertOwner::Speaker, p.layout.speakers[0].id));
}
