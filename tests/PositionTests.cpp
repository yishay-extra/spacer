#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/Layout.h"
#include "core/Position.h"

using Catch::Approx;
using namespace spacer;

TEST_CASE("Azimuth wraps into (-180, 180]") {
    CHECK(Position::wrapAzimuth(0.0f) == Approx(0.0f));
    CHECK(Position::wrapAzimuth(180.0f) == Approx(180.0f));
    CHECK(Position::wrapAzimuth(-180.0f) == Approx(180.0f));
    CHECK(Position::wrapAzimuth(190.0f) == Approx(-170.0f));
    CHECK(Position::wrapAzimuth(-190.0f) == Approx(170.0f));
    CHECK(Position::wrapAzimuth(720.0f) == Approx(0.0f));
}

TEST_CASE("Spherical to Cartesian follows the spec convention") {
    const float eps = 1e-5f;
    SECTION("front") {
        auto c = Position{0.0f, 0.0f, 2.0f}.toCartesian();
        CHECK(c.x == Approx(0.0f).margin(eps));
        CHECK(c.y == Approx(2.0f).margin(eps));
        CHECK(c.z == Approx(0.0f).margin(eps));
    }
    SECTION("right is +90 azimuth") {
        auto c = Position{90.0f, 0.0f, 1.0f}.toCartesian();
        CHECK(c.x == Approx(1.0f).margin(eps));
        CHECK(c.y == Approx(0.0f).margin(eps));
    }
    SECTION("left is -90 azimuth") {
        auto c = Position{-90.0f, 0.0f, 1.0f}.toCartesian();
        CHECK(c.x == Approx(-1.0f).margin(eps));
    }
    SECTION("up is +90 elevation") {
        auto c = Position{0.0f, 90.0f, 3.0f}.toCartesian();
        CHECK(c.z == Approx(3.0f).margin(eps));
        CHECK(c.x == Approx(0.0f).margin(eps));
        CHECK(c.y == Approx(0.0f).margin(eps));
    }
}

TEST_CASE("Cartesian round trip") {
    for (float az : {-170.0f, -90.0f, -30.0f, 0.0f, 45.0f, 120.0f, 180.0f})
        for (float el : {-60.0f, 0.0f, 30.0f, 80.0f})
            for (float d : {0.5f, 1.0f, 7.25f}) {
                Position p{az, el, d};
                Position back = Position::fromCartesian(p.toCartesian());
                // Compare direction angularly: raw azimuth is ambiguous at the ±180° seam.
                CHECK(angleBetween(p, back) < 1e-3f);
                CHECK(back.elevation == Approx(el).margin(1e-3f));
                CHECK(back.distance == Approx(d).margin(1e-4f));
            }
}

TEST_CASE("Origin maps to a zero-distance position") {
    auto p = Position::fromCartesian({0.0f, 0.0f, 0.0f});
    CHECK(p.distance == 0.0f);
}

TEST_CASE("lerp passes through endpoints and midpoint") {
    Position a{-90.0f, 0.0f, 1.0f};
    Position b{90.0f, 0.0f, 1.0f};
    CHECK(lerp(a, b, 0.0f).azimuth == Approx(-90.0f).margin(1e-3f));
    CHECK(lerp(a, b, 1.0f).azimuth == Approx(90.0f).margin(1e-3f));
    // Midpoint of left and right at equal distance is the origin.
    CHECK(lerp(a, b, 0.5f).distance == Approx(0.0f).margin(1e-5f));
}

TEST_CASE("angleBetween") {
    CHECK(angleBetween({0, 0, 1}, {90, 0, 1}) == Approx(90.0f).margin(1e-3f));
    CHECK(angleBetween({0, 0, 1}, {180, 0, 5}) == Approx(180.0f).margin(1e-3f));
    CHECK(angleBetween({30, 10, 1}, {30, 10, 2}) == Approx(0.0f).margin(1e-3f));
}

TEST_CASE("Compensation delays nearer speakers and attenuates them") {
    Layout layout;
    layout.speakers = {
        {1, "near", {0.0f, 0.0f, 1.0f}},
        {2, "far", {90.0f, 0.0f, 2.0f}},
    };
    auto comp = computeCompensation(layout, 343.0f);
    REQUIRE(comp.size() == 2);
    // Far speaker: reference, no delay, 0 dB.
    CHECK(comp[1].delayMs == Approx(0.0f));
    CHECK(comp[1].gainDb == Approx(0.0f));
    // Near speaker: 1 m closer → ~2.915 ms delay, 1/r → -6.02 dB.
    CHECK(comp[0].delayMs == Approx(1000.0f / 343.0f).margin(1e-3f));
    CHECK(comp[0].gainDb == Approx(-6.0206f).margin(1e-3f));
}
