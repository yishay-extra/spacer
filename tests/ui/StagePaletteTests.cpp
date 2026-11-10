#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>

#include "ui/logic/StagePalette.h"

using namespace spacer;

namespace {
float hueGap(float a, float b) {
    const float d = std::abs(a - b);
    return std::min(d, 1.0f - d);
}
}  // namespace

TEST_CASE("Every source id has its own stable colour", "[ui][palette]") {
    REQUIRE(sourceColour(7) == sourceColour(7));
    for (int a = 1; a <= 64; ++a) {
        REQUIRE(sourceHue(a) >= 0.0f);
        REQUIRE(sourceHue(a) < 1.0f);
        for (int b = a + 1; b <= 64; ++b) {
            REQUIRE(hueGap(sourceHue(a), sourceHue(b)) > 0.005f);
            REQUIRE_FALSE(sourceColour(a) == sourceColour(b));
        }
    }
    for (int a = 1; a < 64; ++a) REQUIRE(hueGap(sourceHue(a), sourceHue(a + 1)) > 0.3f);  // neighbours contrast
    for (int id : {0, -5}) {
        REQUIRE(sourceHue(id) >= 0.0f);
        REQUIRE(sourceHue(id) < 1.0f);
    }
}

TEST_CASE("Source colours are bright and saturated", "[ui][palette]") {
    for (int id = 1; id <= 64; ++id) {
        const StageRgb c = sourceColour(id);
        REQUIRE(std::max({c.r, c.g, c.b}) == 255);                  // value 1.0
        REQUIRE(std::min({c.r, c.g, c.b}) == 89);                   // 255 × (1 − 0.65), rounded
    }
}
