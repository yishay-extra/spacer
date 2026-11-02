#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/LayoutPresets.h"
#include "ui/logic/StageTransform.h"

using namespace spacer;
using Catch::Matchers::WithinAbs;

namespace {

StageTransform square(StageProjection view = StageProjection::Top) {
    StageTransform t;
    t.setViewport({0, 0, 400, 400});
    t.setExtent(2.0f);  // 2 m from centre to edge -> 100 px/m
    t.setView(view);
    return t;
}

}  // namespace

TEST_CASE("Top view puts front up and right to the right", "[ui][stage]") {
    const auto t = square();
    REQUIRE_THAT(t.pixelsPerMetre(), WithinAbs(100.0, 1e-4));
    const auto origin = t.toScreen({0, 0, 0});
    REQUIRE_THAT(origin.x, WithinAbs(200.0, 1e-4));
    REQUIRE_THAT(origin.y, WithinAbs(200.0, 1e-4));
    // A speaker straight ahead (az 0) is above the centre.
    const auto front = t.toScreen(Position{0, 0, 2}.toCartesian());
    REQUIRE_THAT(front.x, WithinAbs(200.0, 1e-3));
    REQUIRE_THAT(front.y, WithinAbs(0.0, 1e-3));
    // az +90 is to the right (SPEC §2: positive = clockwise from above).
    const auto right = t.toScreen(Position{90, 0, 1}.toCartesian());
    REQUIRE_THAT(right.x, WithinAbs(300.0, 1e-3));
    REQUIRE_THAT(right.y, WithinAbs(200.0, 1e-3));
}

TEST_CASE("Side view shows front to the right and up as up", "[ui][stage]") {
    const auto t = square(StageProjection::Side);
    const auto front = t.toScreen(Position{0, 0, 1}.toCartesian());
    REQUIRE_THAT(front.x, WithinAbs(300.0, 1e-3));
    REQUIRE_THAT(front.y, WithinAbs(200.0, 1e-3));
    const auto zenith = t.toScreen(Position{0, 90, 1}.toCartesian());
    REQUIRE_THAT(zenith.x, WithinAbs(200.0, 1e-3));
    REQUIRE_THAT(zenith.y, WithinAbs(100.0, 1e-3));
    // x (right) is the depth of the side view: it does not move the point.
    const auto right = t.toScreen(Position{90, 0, 1}.toCartesian());
    REQUIRE_THAT(right.x, WithinAbs(200.0, 1e-3));
    REQUIRE_THAT(t.depthOf(Position{90, 0, 1}.toCartesian()), WithinAbs(1.0, 1e-5));
}

TEST_CASE("Screen to world inverts world to screen and keeps the depth", "[ui][stage]") {
    for (auto view : {StageProjection::Top, StageProjection::Side}) {
        auto t = square(view);
        t.setViewport({30, 40, 640, 380});
        t.setZoom(1.7f);
        t.setPan({0.4f, -0.3f});
        const Cartesian p{1.25f, -0.75f, 0.5f};
        const auto screen = t.toScreen(p);
        const Cartesian back = t.toWorld(screen, p);
        REQUIRE_THAT(back.x, WithinAbs(p.x, 1e-4));
        REQUIRE_THAT(back.y, WithinAbs(p.y, 1e-4));
        REQUIRE_THAT(back.z, WithinAbs(p.z, 1e-4));
    }
    // The hidden coordinate comes from the reference point.
    const auto top = square();
    REQUIRE_THAT(top.toWorld({200, 200}, {9, 9, 1.5f}).z, WithinAbs(1.5, 1e-6));
    const auto side = square(StageProjection::Side);
    REQUIRE_THAT(side.toWorld({200, 200}, {-2.0f, 9, 9}).x, WithinAbs(-2.0, 1e-6));
}

TEST_CASE("Screen deltas map to world deltas on the shown axes only", "[ui][stage]") {
    const auto top = square();
    const Cartesian d = top.screenDeltaToWorld(50.0f, -100.0f);  // right and up on screen
    REQUIRE_THAT(d.x, WithinAbs(0.5, 1e-5));
    REQUIRE_THAT(d.y, WithinAbs(1.0, 1e-5));
    REQUIRE_THAT(d.z, WithinAbs(0.0, 1e-6));
    const auto side = square(StageProjection::Side);
    const Cartesian s = side.screenDeltaToWorld(50.0f, -100.0f);
    REQUIRE_THAT(s.x, WithinAbs(0.0, 1e-6));
    REQUIRE_THAT(s.y, WithinAbs(0.5, 1e-5));
    REQUIRE_THAT(s.z, WithinAbs(1.0, 1e-5));
}

TEST_CASE("Non-square viewports scale by the shorter side", "[ui][stage]") {
    StageTransform t;
    t.setViewport({0, 0, 800, 200});
    t.setExtent(2.0f);
    REQUIRE_THAT(t.pixelsPerMetre(), WithinAbs(50.0, 1e-4));
}

TEST_CASE("Zooming about a point keeps that point fixed", "[ui][stage]") {
    auto t = square();
    const StagePoint anchor{320, 90};
    const Cartesian before = t.toWorld(anchor);
    t.zoomAbout(anchor, 2.5f);
    REQUIRE_THAT(t.zoom(), WithinAbs(2.5, 1e-6));
    const Cartesian after = t.toWorld(anchor);
    REQUIRE_THAT(after.x, WithinAbs(before.x, 1e-4));
    REQUIRE_THAT(after.y, WithinAbs(before.y, 1e-4));
    t.zoomAbout(anchor, 1000.0f);
    REQUIRE_THAT(t.zoom(), WithinAbs(StageTransform::kMaxZoom, 1e-6));
    t.setZoom(0.0f);
    REQUIRE_THAT(t.zoom(), WithinAbs(StageTransform::kMinZoom, 1e-6));
}

TEST_CASE("Fit extent covers every speaker with a margin", "[ui][stage]") {
    const auto dome = layoutpresets::load("dome22");
    REQUIRE(dome);
    const float e = StageTransform::fitExtent(*dome);
    REQUIRE_THAT(e, WithinAbs(2.0 * 1.15, 1e-3));  // all presets at 2 m
    Layout tiny;
    tiny.speakers = {Speaker{1, "a", {0, 0, 0.2f}}, Speaker{2, "b", {90, 0, 0.2f}}};
    REQUIRE_THAT(StageTransform::fitExtent(tiny), WithinAbs(StageTransform::kMinExtentMetres, 1e-6));
    StageTransform t;
    t.setExtent(-3.0f);
    REQUIRE_THAT(t.extent(), WithinAbs(StageTransform::kMinExtentMetres, 1e-6));
}
