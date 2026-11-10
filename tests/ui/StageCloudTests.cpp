#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <limits>

#include "ui/logic/StageCloud.h"

using namespace spacer;
using Catch::Matchers::WithinAbs;

TEST_CASE("Cloud offsets are one fixed, centred, unit-sized table", "[ui][cloud]") {
    const auto o = cloudOffsets();
    REQUIRE(o.size() == static_cast<std::size_t>(kCloudMaxDots));
    REQUIRE(o.data() == cloudOffsets().data());
    REQUIRE_THAT(o[0].x, WithinAbs(0.978417, 1e-4));
    REQUIRE_THAT(o[0].y, WithinAbs(2.024173, 1e-4));
    REQUIRE_THAT(o[0].z, WithinAbs(-1.712411, 1e-4));
    REQUIRE_THAT(o[0].phase, WithinAbs(1.936542, 1e-4));
    REQUIRE_THAT(o[95].x, WithinAbs(0.880879, 1e-4));
    REQUIRE_THAT(o[95].phase, WithinAbs(3.399481, 1e-4));
    double mx = 0, my = 0, mz = 0, sq = 0;
    for (const auto& p : o) {
        mx += p.x; my += p.y; mz += p.z;
        sq += p.x * p.x + p.y * p.y + p.z * p.z;
        REQUIRE(std::abs(p.x) <= 3.47f);
        REQUIRE(std::abs(p.y) <= 3.47f);
        REQUIRE(std::abs(p.z) <= 3.47f);
        REQUIRE(p.phase >= 0.0f);
        REQUIRE(p.phase < 6.2832f);
    }
    REQUIRE_THAT(mx / 96.0, WithinAbs(0.041, 0.01));
    REQUIRE_THAT(my / 96.0, WithinAbs(0.085, 0.01));
    REQUIRE_THAT(mz / 96.0, WithinAbs(-0.074, 0.01));
    REQUIRE_THAT(sq / (3.0 * 96.0), WithinAbs(0.897, 0.01));
}

TEST_CASE("The dot limit shares the frame budget", "[ui][cloud]") {
    REQUIRE(cloudDotLimit(-3) == 96);
    REQUIRE(cloudDotLimit(0) == 96);
    REQUIRE(cloudDotLimit(1) == 96);
    REQUIRE(cloudDotLimit(62) == 96);
    REQUIRE(cloudDotLimit(63) == 95);
    REQUIRE(cloudDotLimit(128) == 46);  // 64 stereo sources, all loud
    REQUIRE(cloudDotLimit(250) == 24);
    REQUIRE(cloudDotLimit(100000) == 24);
}

TEST_CASE("Louder clouds have more dots", "[ui][cloud]") {
    REQUIRE(cloudDots(0.0f, 96) == 0);
    REQUIRE(cloudDots(1.0f, 96) == 96);
    REQUIRE(cloudDots(0.5f, 96) == 48);
    REQUIRE(cloudDots(0.001f, 96) == 1);
    REQUIRE(cloudDots(2.0f, 96) == 96);
    REQUIRE(cloudDots(-1.0f, 96) == 0);
    REQUIRE(cloudDots(std::numeric_limits<float>::quiet_NaN(), 96) == 0);
    REQUIRE(cloudDots(0.5f, 0) == 0);
    int previous = 0;
    for (int i = 0; i <= 20; ++i) {
        const int n = cloudDots(static_cast<float>(i) * 0.05f, 46);
        REQUIRE(n >= previous);
        previous = n;
    }
}

TEST_CASE("Speaker glows share their own frame budget", "[ui][cloud]") {
    REQUIRE(glowDotLimit(96, 0) == 24);
    REQUIRE(glowDotLimit(96, 1) == 24);
    REQUIRE(glowDotLimit(96, 125) == 24);
    REQUIRE(glowDotLimit(96, 126) == 23);
    REQUIRE(glowDotLimit(96, 300) == 10);
    REQUIRE(glowDotLimit(96, 2048) == 3);
    REQUIRE(glowDotLimit(46, 2048) == 3);
    REQUIRE(glowDotLimit(24, 10) == 6);
    REQUIRE(glowDotLimit(8, 4000) == 2);
    REQUIRE(glowDotLimit(0, 5) == 0);
}
