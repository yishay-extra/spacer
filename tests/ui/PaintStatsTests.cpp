#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "ui/logic/PaintStats.h"

using namespace spacer;
using Catch::Matchers::WithinAbs;

TEST_CASE("Paint stats cover the last 60 paints", "[ui][paintstats]") {
    PaintStats s;
    REQUIRE(s.count() == 0);
    REQUIRE(s.meanMs() == 0.0);
    REQUIRE(s.maxMs() == 0.0);
    for (int i = 1; i <= 60; ++i) s.add(i);
    REQUIRE(s.count() == 60);
    REQUIRE_THAT(s.meanMs(), WithinAbs(30.5, 1e-9));
    REQUIRE_THAT(s.maxMs(), WithinAbs(60.0, 1e-9));
    s.add(100.0);  // pushes out the 1
    REQUIRE(s.count() == 60);
    REQUIRE_THAT(s.meanMs(), WithinAbs(1929.0 / 60.0, 1e-9));
    REQUIRE_THAT(s.maxMs(), WithinAbs(100.0, 1e-9));
    for (int i = 0; i < 60; ++i) s.add(2.0);  // the 100 has left the window
    REQUIRE_THAT(s.maxMs(), WithinAbs(2.0, 1e-9));
}
