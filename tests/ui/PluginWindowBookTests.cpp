#include <catch2/catch_test_macros.hpp>
#include <map>

#include "core/LayoutPresets.h"
#include "ui/logic/PluginWindowBook.h"

using namespace spacer;
using namespace spacer::pluginwindows;
using Open = WindowBook::OpenResult;

namespace {

struct Fixture {
    Project project;
    std::map<std::tuple<int, int, int>, InstanceToken> live;  // (owner, id, slot) -> instance
    int speaker{0};

    Fixture() {
        project.layout = *layoutpresets::load("quad");
        speaker = project.layout.speakers[0].id;
        SourceSpec s;
        s.id = 1;
        s.inserts = {PluginSlot{"A", "", {}, false}, PluginSlot{"B", "", {}, false}};
        project.sources.push_back(s);
        SpeakerChainSpec c;
        c.speakerId = speaker;
        c.inserts = {PluginSlot{"S", "", {}, false}};
        project.speakerChains.push_back(c);
        set(source(0), 100);
        set(source(1), 101);
        set(spk(0), 200);
    }
    static SlotKey source(int slot) { return {InsertOwner::Source, 1, slot}; }
    SlotKey spk(int slot) const { return {InsertOwner::Speaker, speaker, slot}; }
    void set(const SlotKey& k, InstanceToken t) { live[{static_cast<int>(k.owner), k.ownerId, k.slot}] = t; }
    void gone(const SlotKey& k) { live.erase({static_cast<int>(k.owner), k.ownerId, k.slot}); }
    InstanceLookup lookup() const {
        return [this](const SlotKey& k) -> InstanceToken {
            const auto it = live.find({static_cast<int>(k.owner), k.ownerId, k.slot});
            return it == live.end() ? 0 : it->second;
        };
    }
};

}  // namespace

TEST_CASE("Window book: one window per slot, source and speaker slots are distinct", "[ui][plugins]") {
    Fixture f;
    WindowBook book;
    CHECK(book.open(f.project, Fixture::source(0), f.lookup()) == Open::Opened);
    CHECK(book.open(f.project, Fixture::source(0), f.lookup()) == Open::AlreadyOpen);
    CHECK(book.open(f.project, f.spk(0), f.lookup()) == Open::Opened);
    // Source 1 slot 0 and speaker 1 slot 0 have the same numbers but are different slots.
    CHECK(Fixture::source(0).ownerId == f.spk(0).ownerId);
    CHECK(Fixture::source(0).slot == f.spk(0).slot);
    CHECK(book.entries().size() == 2);
    CHECK(book.find(Fixture::source(0))->pluginId == "A");
    CHECK(book.find(f.spk(0))->instance == 200);
}

TEST_CASE("Window book: opening refuses empty slots and plug-ins that are not loaded", "[ui][plugins]") {
    Fixture f;
    WindowBook book;
    CHECK(book.open(f.project, Fixture::source(7), f.lookup()) == Open::NoSuchSlot);
    CHECK(book.open(f.project, {InsertOwner::Source, 99, 0}, f.lookup()) == Open::NoSuchSlot);
    f.project.sources[0].inserts.push_back(PluginSlot{});  // an empty slot
    CHECK(book.open(f.project, Fixture::source(2), f.lookup()) == Open::NoSuchSlot);
    f.gone(Fixture::source(1));  // plug-in missing / failed to load
    CHECK(book.open(f.project, Fixture::source(1), f.lookup()) == Open::NotLoaded);
    CHECK(book.entries().empty());
}

TEST_CASE("Window book: a window follows the instance across a graph swap", "[ui][plugins]") {
    Fixture f;
    WindowBook book;
    book.open(f.project, Fixture::source(0), f.lookup());
    CHECK(book.reconcile(f.project, f.lookup()).empty());  // nothing changed

    f.set(Fixture::source(0), 150);  // recall / state change replaced the instance
    const auto plan = book.reconcile(f.project, f.lookup());
    CHECK(plan.close.empty());
    REQUIRE(plan.retarget.size() == 1);
    CHECK(plan.retarget[0].key == Fixture::source(0));
    CHECK(plan.retarget[0].instance == 150);
    CHECK(book.find(Fixture::source(0))->instance == 150);
    CHECK(book.reconcile(f.project, f.lookup()).empty());  // and only once
}

TEST_CASE("Window book: windows close when the slot or its plug-in goes away", "[ui][plugins]") {
    Fixture f;
    WindowBook book;
    book.open(f.project, Fixture::source(0), f.lookup());
    book.open(f.project, Fixture::source(1), f.lookup());
    book.open(f.project, f.spk(0), f.lookup());

    // Slot 0 removed: slot 1 ("B") moves into index 0, where the window showed "A".
    f.project.sources[0].inserts.erase(f.project.sources[0].inserts.begin());
    f.set(Fixture::source(0), 300);
    f.gone(Fixture::source(1));
    auto plan = book.reconcile(f.project, f.lookup());
    CHECK(plan.retarget.empty());
    REQUIRE(plan.close.size() == 2);  // index 0 now holds another plug-in, index 1 no longer exists
    CHECK(plan.close[0] == Fixture::source(0));
    CHECK(plan.close[1] == Fixture::source(1));
    CHECK(book.entries().size() == 1);

    // The speaker is removed from the layout (its chain goes with it).
    f.project.speakerChains.clear();
    plan = book.reconcile(f.project, f.lookup());
    REQUIRE(plan.close.size() == 1);
    CHECK(plan.close[0] == f.spk(0));
    CHECK(book.entries().empty());
}

TEST_CASE("Window book: an instance that is gone closes its window", "[ui][plugins]") {
    Fixture f;
    WindowBook book;
    book.open(f.project, Fixture::source(0), f.lookup());
    f.gone(Fixture::source(0));  // e.g. the plug-in failed to load after a rebuild
    const auto plan = book.reconcile(f.project, f.lookup());
    REQUIRE(plan.close.size() == 1);
    CHECK(book.entries().empty());
}

TEST_CASE("Window book: a replaced plug-in in the same slot closes the old window", "[ui][plugins]") {
    Fixture f;
    WindowBook book;
    book.open(f.project, Fixture::source(0), f.lookup());
    f.project.sources[0].inserts[0].pluginId = "C";
    f.set(Fixture::source(0), 400);
    const auto plan = book.reconcile(f.project, f.lookup());
    CHECK(plan.close.size() == 1);
    CHECK(plan.retarget.empty());
}

TEST_CASE("Window book: closing by the user forgets the slot", "[ui][plugins]") {
    Fixture f;
    WindowBook book;
    book.open(f.project, Fixture::source(0), f.lookup());
    book.closed(Fixture::source(0));
    CHECK_FALSE(book.isOpen(Fixture::source(0)));
    CHECK(book.open(f.project, Fixture::source(0), f.lookup()) == Open::Opened);  // can open again
    book.closed(Fixture::source(5));                                              // unknown: harmless
    CHECK(book.entries().size() == 1);
}
