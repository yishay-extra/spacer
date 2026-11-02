#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/LayoutPresets.h"
#include "ui/logic/SourceGeometry.h"

using namespace spacer;
using Catch::Matchers::WithinAbs;

namespace {

void requireNear(const Cartesian& a, const Cartesian& b, double tol = 1e-4) {
    REQUIRE_THAT(a.x, WithinAbs(b.x, tol));
    REQUIRE_THAT(a.y, WithinAbs(b.y, tol));
    REQUIRE_THAT(a.z, WithinAbs(b.z, tol));
}

SourceSpec manualSource(Position p) {
    SourceSpec s;
    s.id = 4;
    s.motion.value = ManualMotion{p, 20.0f};
    return s;
}

}  // namespace

TEST_CASE("Dragging a Manual source sets its position", "[ui][drag]") {
    const auto start = manualSource({0, 0, 2});  // (0, 2, 0)
    const auto moved = dragSource(start, 0, {1.0f, -1.0f, 0.0f});
    const auto& m = std::get<ManualMotion>(moved.motion.value);
    requireNear(m.position.toCartesian(), {1, 1, 0});
    REQUIRE_THAT(m.position.azimuth, WithinAbs(45.0, 1e-3));
    REQUIRE(m.smoothMs == 20.0f);
}

TEST_CASE("Dragging a moving source offsets its motion centre", "[ui][drag]") {
    SourceSpec s;
    s.motion.value = OrbitMotion{{0, 0, 0}, 1.5f, Period::beats(8), 1, 0, 0, Period::beats(8)};
    auto moved = dragSource(s, 0, {0.0f, 1.0f, 0.5f});
    const auto& orbit = std::get<OrbitMotion>(moved.motion.value);
    requireNear(orbit.centre.toCartesian(), {0, 1, 0.5f});
    REQUIRE(orbit.radius == 1.5f);  // only placement changes

    s.motion.value = LineMotion{};
    const auto line0 = std::get<LineMotion>(s.motion.value);
    moved = dragSource(s, 0, {0.5f, 0.0f, 0.0f});
    const auto& line = std::get<LineMotion>(moved.motion.value);
    requireNear(line.from.toCartesian(), line0.from.toCartesian() + Cartesian{0.5f, 0, 0});
    requireNear(line.to.toCartesian(), line0.to.toCartesian() + Cartesian{0.5f, 0, 0});
}

TEST_CASE("Offsetting moves every placed part of each motion kind", "[ui][drag]") {
    const Cartesian d{0.25f, -0.5f, 1.0f};

    PathMotion path;
    path.points = {{0, 1, 0}, {1, 1, 0}};
    const auto p = std::get<PathMotion>(offsetMotion({path}, d).value);
    requireNear(p.points[0], Cartesian{0, 1, 0} + d);
    requireNear(p.points[1], Cartesian{1, 1, 0} + d);

    RandomWalkMotion box;
    box.bounds = BoxBounds{{-1, -1, 0}, {1, 1, 1}};
    const auto b = std::get<BoxBounds>(std::get<RandomWalkMotion>(offsetMotion({box}, d).value).bounds);
    requireNear(b.min, Cartesian{-1, -1, 0} + d);
    requireNear(b.max, Cartesian{1, 1, 1} + d);

    RandomWalkMotion sphere;
    const auto sp =
        std::get<SphereBounds>(std::get<RandomWalkMotion>(offsetMotion({sphere}, d).value).bounds);
    requireNear(sp.centre, d);

    LissajousMotion lis;
    lis.centre = {0, 0, 1};
    const auto l = std::get<LissajousMotion>(offsetMotion({lis}, d).value);
    requireNear(l.centre.toCartesian(), Cartesian{0, 1, 0} + d);

    StepSequenceMotion seq;
    seq.steps = {Step{Position{0, 0, 1}}, Step{5}};
    const auto q = std::get<StepSequenceMotion>(offsetMotion({seq}, d).value);
    requireNear(std::get<Position>(q.steps[0].target).toCartesian(), Cartesian{0, 1, 0} + d);
    REQUIRE(std::get<int>(q.steps[1].target) == 5);  // speaker targets stay on their speaker
}

TEST_CASE("Stereo points sit at azimuth plus and minus half the width", "[ui][drag]") {
    SourceSpec s = manualSource({10, 5, 2});
    s.stereo = true;
    s.width = 60.0f;
    const auto pts = modelPoints(s, Layout{});
    REQUIRE(pts.size() == 2);
    REQUIRE_THAT(pts[0].azimuth, WithinAbs(-20.0, 1e-4));
    REQUIRE_THAT(pts[1].azimuth, WithinAbs(40.0, 1e-4));
    REQUIRE(pts[0].elevation == 5.0f);
    REQUIRE(pts[1].distance == 2.0f);
    // Wraps across 180.
    REQUIRE_THAT(stereoPoint({170, 0, 1}, 40.0f, 1, true).azimuth, WithinAbs(-170.0, 1e-3));
    REQUIRE(stereoPoint({170, 0, 1}, 40.0f, 1, false) == Position{170, 0, 1});
}

TEST_CASE("Dragging either point of a stereo pair moves the pair", "[ui][drag]") {
    SourceSpec s = manualSource({0, 0, 2});
    s.stereo = true;
    const auto moved = dragSource(s, 1, {0.0f, 1.0f, 0.0f});
    requireNear(std::get<ManualMotion>(moved.motion.value).position.toCartesian(), {0, 3, 0});
    REQUIRE_FALSE(moved.motionRight);
}

TEST_CASE("Linked points move independently", "[ui][drag]") {
    SourceSpec s = manualSource({-30, 0, 2});
    s.stereo = true;
    s.linkedPoints = true;
    s.motionRight = MotionSpec{ManualMotion{{30, 0, 2}, 20.0f}};
    const auto moved = dragSource(s, 1, {0.0f, 0.0f, 1.0f});
    REQUIRE(moved.motion == s.motion);
    const auto& right = std::get<ManualMotion>(moved.motionRight->value).position;
    requireNear(right.toCartesian(), Position{30, 0, 2}.toCartesian() + Cartesian{0, 0, 1});
    // Model points of linked sources carry no width offset.
    const auto pts = modelPoints(s, Layout{});
    REQUIRE(pts[0].azimuth == -30.0f);
    REQUIRE(pts[1].azimuth == 30.0f);
    // Without motionRight the right point follows `motion` and gets one when dragged.
    s.motionRight.reset();
    const auto m2 = dragSource(s, 1, {0.0f, 0.0f, 1.0f});
    REQUIRE(m2.motionRight);
    REQUIRE(m2.motion == s.motion);
}

TEST_CASE("Manual-only detection covers linked right points", "[ui][drag]") {
    SourceSpec s = manualSource({});
    REQUIRE(isManualOnly(s));
    s.stereo = true;
    s.linkedPoints = true;
    s.motionRight = MotionSpec{OrbitMotion{}};
    REQUIRE_FALSE(isManualOnly(s));
    s.motionRight.reset();
    REQUIRE(isManualOnly(s));
    s.motion.value = OrbitMotion{};
    REQUIRE_FALSE(isManualOnly(s));
}

TEST_CASE("Model points of a moving source are its phase-0 position", "[ui][drag]") {
    SourceSpec s;
    s.motion.value = LineMotion{{-90, 0, 2}, {90, 0, 2}, Period::beats(4), PlayMode::Once, Easing::Linear};
    const auto pts = modelPoints(s, Layout{});
    REQUIRE(pts.size() == 1);
    requireNear(pts[0].toCartesian(), Position{-90, 0, 2}.toCartesian(), 1e-3);
}
