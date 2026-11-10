#include <cmath>
#include <limits>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/LayoutPresets.h"
#include "ui/logic/StageTransform.h"

using namespace spacer;
using Catch::Matchers::WithinAbs;

namespace {

StageTransform square(StageSnap view = StageSnap::Top) {
    StageTransform t;
    t.setViewport({0, 0, 400, 400});
    t.setExtent(2.0f);  // 2 m from centre to edge -> 100 px/m
    t.setSnap(view);
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
    const auto t = square(StageSnap::Side);
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
    for (auto view : {StageSnap::Top, StageSnap::Side}) {
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
    const auto side = square(StageSnap::Side);
    REQUIRE_THAT(side.toWorld({200, 200}, {-2.0f, 9, 9}).x, WithinAbs(-2.0, 1e-6));
}

TEST_CASE("Screen deltas map to world deltas on the shown axes only", "[ui][stage]") {
    const auto top = square();
    const Cartesian d = top.screenDeltaToWorld(50.0f, -100.0f);  // right and up on screen
    REQUIRE_THAT(d.x, WithinAbs(0.5, 1e-5));
    REQUIRE_THAT(d.y, WithinAbs(1.0, 1e-5));
    REQUIRE_THAT(d.z, WithinAbs(0.0, 1e-6));
    const auto side = square(StageSnap::Side);
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

TEST_CASE("Snap cameras give exact axis-aligned bases", "[ui][stage]") {
    StageTransform t;
    REQUIRE(t.snap() == StageSnap::Top);  // the default
    REQUIRE(t.screenRight() == Cartesian{1, 0, 0});
    REQUIRE(t.screenUp() == Cartesian{0, 1, 0});
    REQUIRE(t.towardViewer() == Cartesian{0, 0, 1});
    t.setSnap(StageSnap::Side);
    REQUIRE(t.screenRight() == Cartesian{0, 1, 0});
    REQUIRE(t.screenUp() == Cartesian{0, 0, 1});
    REQUIRE(t.towardViewer() == Cartesian{1, 0, 0});
    t.setSnap(StageSnap::Front);
    REQUIRE(t.screenRight() == Cartesian{1, 0, 0});
    REQUIRE(t.screenUp() == Cartesian{0, 0, 1});
    REQUIRE(t.towardViewer() == Cartesian{0, -1, 0});
    REQUIRE(StageTransform::cameraFor(StageSnap::ThreeD) == StageCamera{-32.0f, 24.0f});
}

TEST_CASE("Every camera has an orthonormal basis with a level floor", "[ui][stage]") {
    StageTransform t;
    for (float yaw : {-170.0f, -32.0f, 0.0f, 45.0f, 120.0f})
        for (float pitch : {-90.0f, -40.0f, 0.0f, 24.0f, 90.0f}) {
            t.setCamera({yaw, pitch});
            const Cartesian r = t.screenRight(), u = t.screenUp(), d = t.towardViewer();
            REQUIRE_THAT(r.length(), WithinAbs(1.0, 1e-5));
            REQUIRE_THAT(u.length(), WithinAbs(1.0, 1e-5));
            REQUIRE_THAT(d.length(), WithinAbs(1.0, 1e-5));
            REQUIRE_THAT(r.dot(u), WithinAbs(0.0, 1e-5));
            REQUIRE_THAT(r.dot(d), WithinAbs(0.0, 1e-5));
            REQUIRE_THAT(u.dot(d), WithinAbs(0.0, 1e-5));
            REQUIRE_THAT(r.cross(u).dot(d), WithinAbs(1.0, 1e-5));
            REQUIRE(r.z == 0.0f);   // the view never rolls
            REQUIRE(u.z >= 0.0f);   // world up never draws downwards
        }
}

TEST_CASE("Screen and world round-trip at a free camera angle", "[ui][stage]") {
    auto t = square();
    t.setViewport({30, 40, 640, 380});
    t.setZoom(1.7f);
    t.setPan({0.4f, -0.3f});
    t.setCamera({-32.0f, 24.0f});
    const Cartesian p{1.25f, -0.75f, 0.5f};
    const Cartesian back = t.toWorld(t.toScreen(p), p);
    REQUIRE_THAT(back.x, WithinAbs(p.x, 1e-4));
    REQUIRE_THAT(back.y, WithinAbs(p.y, 1e-4));
    REQUIRE_THAT(back.z, WithinAbs(p.z, 1e-4));
    // A drag moves the point in the screen plane and follows the mouse.
    const Cartesian moved = p + t.screenDeltaToWorld(37.0f, -12.0f);
    REQUIRE_THAT(t.depthOf(moved), WithinAbs(t.depthOf(p), 1e-4));
    REQUIRE_THAT(t.toScreen(moved).x - t.toScreen(p).x, WithinAbs(37.0, 1e-2));
    REQUIRE_THAT(t.toScreen(moved).y - t.toScreen(p).y, WithinAbs(-12.0, 1e-2));
}

TEST_CASE("Orbit turns 0.4 degrees per pixel, wraps yaw and clamps pitch", "[ui][stage]") {
    StageTransform t;
    t.setCamera({0.0f, 0.0f});
    t.orbit(10.0f, 25.0f);
    REQUIRE_THAT(t.camera().yawDeg, WithinAbs(-4.0, 1e-4));
    REQUIRE_THAT(t.camera().pitchDeg, WithinAbs(10.0, 1e-4));
    REQUIRE_FALSE(t.snap());
    t.orbit(0.0f, 1000.0f);
    REQUIRE(t.camera().pitchDeg == 90.0f);
    t.orbit(0.0f, -1000.0f);
    REQUIRE(t.camera().pitchDeg == -90.0f);
    t.setCamera({170.0f, 0.0f});
    t.orbit(-50.0f, 0.0f);  // 170 + 20 wraps to -170
    REQUIRE_THAT(t.camera().yawDeg, WithinAbs(-170.0, 1e-3));
    t.setSnap(StageSnap::Side);
    t.setCamera({std::numeric_limits<float>::quiet_NaN(), 0.0f});
    REQUIRE(t.snap() == StageSnap::Side);  // ignored
}

TEST_CASE("Rotating keeps the listener where it is on screen", "[ui][stage]") {
    auto t = square();
    t.zoomAbout({320, 90}, 2.5f);
    const auto before = t.toScreen({0, 0, 0});
    t.orbit(123.0f, -45.0f);
    const auto after = t.toScreen({0, 0, 0});
    REQUIRE_THAT(after.x, WithinAbs(before.x, 1e-3));
    REQUIRE_THAT(after.y, WithinAbs(before.y, 1e-3));
}

TEST_CASE("Fit extent is the farthest speaker distance", "[ui][stage]") {
    Layout box;
    box.speakers = {Speaker{1, "a", Position::fromCartesian({2, 2, 2})},
                    Speaker{2, "b", Position::fromCartesian({-2, -2, 0})}};
    REQUIRE_THAT(StageTransform::fitExtent(box), WithinAbs(std::sqrt(12.0) * 1.15, 1e-3));
}

TEST_CASE("A zero-size viewport still gives finite results", "[ui][stage]") {
    StageTransform t;
    t.setViewport({0, 0, 0, 0});
    t.setCamera({-32.0f, 24.0f});
    const auto s = t.toScreen({1, 2, 3});
    REQUIRE(std::isfinite(s.x));
    REQUIRE(std::isfinite(s.y));
    const Cartesian w = t.toWorld({5, 5}, {1, 2, 3});
    REQUIRE((std::isfinite(w.x) && std::isfinite(w.y) && std::isfinite(w.z)));
}

TEST_CASE("A point is pulled back along its ray until it is inside", "[ui][stage]") {
    const StageRect bounds{0, 0, 400, 300};
    const StagePoint centre{200, 150};
    auto p = pullInside(centre, {250, 100}, bounds);  // already inside
    REQUIRE_THAT(p.x, WithinAbs(250.0, 1e-4));
    REQUIRE_THAT(p.y, WithinAbs(100.0, 1e-4));
    p = pullInside(centre, {200, -50}, bounds);  // above the top edge
    REQUIRE_THAT(p.x, WithinAbs(200.0, 1e-4));
    REQUIRE_THAT(p.y, WithinAbs(0.0, 1e-4));
    p = pullInside(centre, {500, 150}, bounds);  // beyond the right edge
    REQUIRE_THAT(p.x, WithinAbs(400.0, 1e-4));
    REQUIRE_THAT(p.y, WithinAbs(150.0, 1e-4));
    p = pullInside(centre, {600, 450}, bounds);  // through the corner
    REQUIRE_THAT(p.x, WithinAbs(400.0, 1e-4));
    REQUIRE_THAT(p.y, WithinAbs(300.0, 1e-4));
    p = pullInside({-10, -10}, {200, 150}, bounds);  // the start is outside
    REQUIRE_THAT(p.x, WithinAbs(-10.0, 1e-4));
    REQUIRE_THAT(p.y, WithinAbs(-10.0, 1e-4));
    const StageRect offset{50, 40, 100, 100};  // a rectangle not at the origin
    p = pullInside({100, 90}, {100, 500}, offset);
    REQUIRE_THAT(p.y, WithinAbs(140.0, 1e-4));
}
