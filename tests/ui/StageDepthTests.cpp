#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <limits>
#include <vector>

#include "ui/logic/StageDepth.h"

using namespace spacer;
using Catch::Matchers::WithinAbs;

TEST_CASE("Far-to-near order follows the camera and is stable", "[ui][depth]") {
    const std::vector<Cartesian> pts = {{0, 0, 1}, {0, 0, 0}, {0, 0, 2}, {1, 0, 1}, {0, 0, -1}};
    StageTransform t;  // Top: depth is z
    REQUIRE(farToNear(pts, t) == std::vector<std::size_t>{4, 1, 0, 3, 2});
    t.setSnap(StageSnap::Side);  // depth is x
    REQUIRE(farToNear(pts, t) == std::vector<std::size_t>{0, 1, 2, 4, 3});
    t.setCamera({0.0f, -90.0f});  // from underneath: depth is -z
    REQUIRE(farToNear(pts, t) == std::vector<std::size_t>{2, 0, 3, 1, 4});
}

TEST_CASE("Non-finite positions are not drawn", "[ui][depth]") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    const std::vector<Cartesian> pts = {{0, 0, 1}, {nan, 0, 0}, {0, inf, 0}, {0, 0, 0}};
    REQUIRE(farToNear(pts, StageTransform{}) == std::vector<std::size_t>{3, 0});
}

TEST_CASE("Far items are a little smaller", "[ui][depth]") {
    StageTransform t;
    t.setExtent(2.0f);
    REQUIRE_THAT(depthCue({0, 0, 2}, t), WithinAbs(1.0, 1e-6));
    REQUIRE_THAT(depthCue({0, 0, 0}, t), WithinAbs(0.875, 1e-6));
    REQUIRE_THAT(depthCue({0, 0, -2}, t), WithinAbs(kFarScale, 1e-6));
    REQUIRE_THAT(depthCue({0, 0, 50}, t), WithinAbs(1.0, 1e-6));
    REQUIRE_THAT(depthCue({0, 0, -50}, t), WithinAbs(kFarScale, 1e-6));
}
