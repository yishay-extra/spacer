#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/LayoutPresets.h"
#include "ui/logic/TrajectoryPreview.h"

using namespace spacer;
using Catch::Matchers::WithinAbs;

TEST_CASE("Manual motion has no trajectory", "[ui][trajectory]") {
    REQUIRE(previewTrajectory(MotionSpec{}, Layout{}).empty());
    REQUIRE(previewCycleSeconds(MotionSpec{}, Layout{}, 120.0) == 0.0);
}

TEST_CASE("Cycle length follows the period unit and bpm", "[ui][trajectory]") {
    OrbitMotion o;
    o.period = Period::beats(8);
    REQUIRE_THAT(previewCycleSeconds({o}, Layout{}, 120.0), WithinAbs(4.0, 1e-9));
    REQUIRE_THAT(previewCycleSeconds({o}, Layout{}, 60.0), WithinAbs(8.0, 1e-9));
    o.period = Period::seconds(3);
    REQUIRE_THAT(previewCycleSeconds({o}, Layout{}, 60.0), WithinAbs(3.0, 1e-9));

    LineMotion line;
    line.period = Period::seconds(2);
    line.mode = PlayMode::PingPong;
    REQUIRE_THAT(previewCycleSeconds({line}, Layout{}, 120.0), WithinAbs(4.0, 1e-9));
    line.mode = PlayMode::Once;
    REQUIRE_THAT(previewCycleSeconds({line}, Layout{}, 120.0), WithinAbs(2.0, 1e-9));

    StepSequenceMotion seq;
    seq.steps = {Step{Position{}, Period::beats(2)}, Step{Position{}, Period::seconds(1)}};
    REQUIRE_THAT(previewCycleSeconds({seq}, Layout{}, 120.0), WithinAbs(2.0, 1e-9));

    RandomWalkMotion walk;
    walk.stepPeriod = Period::seconds(0.5);
    REQUIRE_THAT(previewCycleSeconds({walk}, Layout{}, 120.0, 10), WithinAbs(5.0, 1e-9));
}

TEST_CASE("Orbit preview is a closed circle of the orbit radius", "[ui][trajectory]") {
    OrbitMotion o;
    o.centre = {0, 0, 0};
    o.radius = 2.0f;
    o.period = Period::seconds(4);
    TrajectoryPreviewOptions opt;
    opt.numPoints = 101;
    const auto pts = previewTrajectory({o}, Layout{}, opt);
    REQUIRE(pts.size() == 101);
    for (const auto& p : pts) {
        REQUIRE_THAT(p.length(), WithinAbs(2.0, 1e-3));
        REQUIRE_THAT(p.z, WithinAbs(0.0, 1e-4));
    }
    // One full period: ends where it started.
    REQUIRE_THAT(pts.front().x, WithinAbs(pts.back().x, 1e-3));
    REQUIRE_THAT(pts.front().y, WithinAbs(pts.back().y, 1e-3));
    // Midway it is on the opposite side.
    REQUIRE_THAT((pts[50] + pts[0]).length(), WithinAbs(0.0, 1e-3));
}

TEST_CASE("Line preview starts at from and reaches to", "[ui][trajectory]") {
    LineMotion line{{-90, 0, 2}, {90, 0, 2}, Period::beats(4), PlayMode::Once, Easing::Linear};
    const auto pts = previewTrajectory({line}, Layout{});
    REQUIRE(pts.size() == TrajectoryPreviewOptions{}.numPoints);
    REQUIRE_THAT(pts.front().x, WithinAbs(-2.0, 1e-3));
    REQUIRE_THAT(pts.back().x, WithinAbs(2.0, 1e-3));
}

TEST_CASE("Preview is deterministic and capped in length", "[ui][trajectory]") {
    RandomWalkMotion walk;
    walk.seed = 42;
    const auto a = previewTrajectory({walk}, Layout{});
    const auto b = previewTrajectory({walk}, Layout{});
    REQUIRE(a == b);
    REQUIRE_FALSE(a.empty());

    OrbitMotion slow;
    slow.period = Period::seconds(1.0e6);
    TrajectoryPreviewOptions opt;
    opt.maxSeconds = 10.0;
    const auto pts = previewTrajectory({slow}, Layout{}, opt);
    REQUIRE(pts.size() == opt.numPoints);
}

TEST_CASE("Step sequences resolve speaker targets through the layout", "[ui][trajectory]") {
    const auto quad = layoutpresets::load("quad");
    REQUIRE(quad);
    StepSequenceMotion seq;
    seq.steps = {Step{quad->speakers[0].id, Period::beats(1)}};
    const Position p = motionStartPosition({seq}, *quad);
    REQUIRE_THAT(angleBetween(p, quad->speakers[0].position), WithinAbs(0.0, 0.5));
}
