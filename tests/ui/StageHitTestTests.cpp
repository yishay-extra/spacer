#include <catch2/catch_test_macros.hpp>

#include "ui/logic/StageHitTest.h"

using namespace spacer;
using Kind = StageItem::Kind;

namespace {

StageTransform top() {
    StageTransform t;
    t.setViewport({0, 0, 400, 400});
    t.setExtent(2.0f);  // 100 px/m, origin at (200, 200)
    return t;
}

}  // namespace

TEST_CASE("Hit test finds the nearest item within its radius", "[ui][hit]") {
    const std::vector<StageItem> items = {
        {Kind::Speaker, 1, 0, {0.0f, 1.0f, 0.0f}},  // screen (200, 100)
        {Kind::Speaker, 2, 0, {1.0f, 0.0f, 0.0f}},  // screen (300, 200)
    };
    const auto t = top();
    auto hit = hitTest(items, t, {205, 104});
    REQUIRE(hit);
    REQUIRE(hit->id == 1);
    hit = hitTest(items, t, {290, 195});
    REQUIRE(hit);
    REQUIRE(hit->id == 2);
    REQUIRE_FALSE(hitTest(items, t, {250, 150}));  // between, out of range
}

TEST_CASE("Sources win over speakers under the mouse", "[ui][hit]") {
    const std::vector<StageItem> items = {
        {Kind::Speaker, 1, 0, {0.0f, 1.0f, 0.0f}},
        {Kind::Source, 7, 1, {0.08f, 1.0f, 0.0f}},  // 8 px right of the speaker
    };
    const auto t = top();
    const auto hit = hitTest(items, t, {200, 100});  // exactly on the speaker
    REQUIRE(hit);
    REQUIRE(hit->kind == Kind::Source);
    REQUIRE(hit->id == 7);
    REQUIRE(hit->channel == 1);
    // Restricting to speakers (layout editing) ignores sources.
    StageHitOptions speakersOnly;
    speakersOnly.only = Kind::Speaker;
    const auto sp = hitTest(items, t, {200, 100}, speakersOnly);
    REQUIRE(sp);
    REQUIRE(sp->id == 1);
}

TEST_CASE("Overlapping items: the one nearer the viewer wins", "[ui][hit]") {
    // Same x/y, different heights: in the top view the higher one is on top.
    const std::vector<StageItem> items = {
        {Kind::Speaker, 1, 0, {0.0f, 1.0f, 0.0f}},
        {Kind::Speaker, 2, 0, {0.0f, 1.0f, 1.0f}},
    };
    const auto hit = hitTest(items, top(), {200, 100});
    REQUIRE(hit);
    REQUIRE(hit->id == 2);
}

TEST_CASE("Hit radius follows the options", "[ui][hit]") {
    const std::vector<StageItem> items = {{Kind::Source, 3, 0, {0.0f, 0.0f, 0.0f}}};
    StageHitOptions o;
    o.sourceRadiusPx = 5.0f;
    REQUIRE_FALSE(hitTest(items, top(), {208, 200}, o));
    o.sourceRadiusPx = 10.0f;
    REQUIRE(hitTest(items, top(), {208, 200}, o));
}

TEST_CASE("Hit test follows the camera", "[ui][hit]") {
    // Two speakers on one vertical line: one spot from above, apart from the front.
    const std::vector<StageItem> items = {
        {Kind::Speaker, 1, 0, {1.0f, 0.0f, 0.0f}},
        {Kind::Speaker, 2, 0, {1.0f, 0.0f, 1.0f}},
    };
    auto t = top();
    REQUIRE(hitTest(items, t, {300, 200})->id == 2);  // the higher one is nearer the viewer
    t.setSnap(StageSnap::Front);
    REQUIRE(hitTest(items, t, {300, 200})->id == 1);
    REQUIRE(hitTest(items, t, {300, 100})->id == 2);
}
