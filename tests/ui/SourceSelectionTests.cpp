#include <catch2/catch_test_macros.hpp>

#include "ui/logic/SourceSelection.h"

using namespace spacer;

namespace {

Project makeProject(std::initializer_list<int> ids) {
    Project p;
    for (int id : ids) {
        SourceSpec s;
        s.id = id;
        p.sources.push_back(s);
    }
    return p;
}

}  // namespace

TEST_CASE("Selection starts empty and reports changes", "[ui][selection]") {
    SourceSelection sel;
    REQUIRE_FALSE(sel.selected());
    REQUIRE(sel.selectedId() == 0);
    REQUIRE(sel.select(4));
    REQUIRE_FALSE(sel.select(4));  // no change
    REQUIRE(sel.selected() == 4);
    REQUIRE(sel.isSelected(4));
    REQUIRE_FALSE(sel.isSelected(5));
    const auto rev = sel.revision();
    REQUIRE(sel.clear());
    REQUIRE(sel.revision() > rev);
    REQUIRE_FALSE(sel.isSelected(0));  // 0 is "none", never selected
    REQUIRE_FALSE(sel.select(-3));     // invalid ids mean none
}

TEST_CASE("Reconcile repairs a selection that no longer exists", "[ui][selection]") {
    SourceSelection sel;
    const Project p = makeProject({2, 5, 9});
    REQUIRE(sel.reconcile(p));  // nothing selected: first
    REQUIRE(sel.selectedId() == 2);
    REQUIRE_FALSE(sel.reconcile(p));
    sel.select(5);
    REQUIRE_FALSE(sel.reconcile(p));
    REQUIRE(sel.selectedId() == 5);

    const Project after = makeProject({2, 9});
    REQUIRE(sel.reconcile(after, 9));  // preferred neighbour
    REQUIRE(sel.selectedId() == 9);
    sel.select(77);
    REQUIRE(sel.reconcile(after, 123));  // fallback missing too: first
    REQUIRE(sel.selectedId() == 2);

    REQUIRE(sel.reconcile(makeProject({})));
    REQUIRE_FALSE(sel.selected());
}

TEST_CASE("The neighbour after a removal is the next, else the previous", "[ui][selection]") {
    const Project p = makeProject({2, 5, 9});
    REQUIRE(neighbourAfterRemoval(p, 2) == 5);
    REQUIRE(neighbourAfterRemoval(p, 5) == 9);
    REQUIRE(neighbourAfterRemoval(p, 9) == 5);
    REQUIRE(neighbourAfterRemoval(p, 77) == 0);
    REQUIRE(neighbourAfterRemoval(makeProject({4}), 4) == 0);
}

TEST_CASE("Stepping moves through the list without wrapping", "[ui][selection]") {
    const Project p = makeProject({2, 5, 9});
    REQUIRE(stepSelection(p, 2, 1) == 5);
    REQUIRE(stepSelection(p, 5, 1) == 9);
    REQUIRE(stepSelection(p, 9, 1) == 9);
    REQUIRE(stepSelection(p, 2, -1) == 2);
    REQUIRE(stepSelection(p, 0, 1) == 2);   // none selected: first
    REQUIRE(stepSelection(p, 0, -1) == 9);  // ... or last when going up
    REQUIRE(stepSelection(makeProject({}), 3, 1) == 0);
}
