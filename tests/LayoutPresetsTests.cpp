#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <map>
#include <set>

#include "core/LayoutPresets.h"
#include "core/Validate.h"

using namespace spacer;

TEST_CASE("All SPEC §7 presets are present") {
    const auto ids = layoutpresets::ids();
    const std::set<std::string> have(ids.begin(), ids.end());
    const std::set<std::string> want{"stereo",  "quad",   "5.1",    "7.1",   "7.1.4",
                                     "octagon", "ring12", "ring16", "dome22"};
    REQUIRE(have == want);
    REQUIRE_FALSE(layoutpresets::load("nonexistent").has_value());
}

TEST_CASE("Presets load, validate, and have the expected sizes") {
    const std::map<std::string, std::size_t> counts{{"stereo", 2},  {"quad", 4},    {"5.1", 6},
                                                    {"7.1", 8},     {"7.1.4", 12},  {"octagon", 8},
                                                    {"ring12", 12}, {"ring16", 16}, {"dome22", 22}};
    for (const auto& [id, count] : counts) {
        INFO(id);
        auto layout = layoutpresets::load(id);
        REQUIRE(layout.has_value());
        REQUIRE(layout->speakers.size() == count);
        const auto issues = validate(*layout);
        for (const auto& i : issues) INFO(i.path << ": " << i.message);
        REQUIRE_FALSE(hasErrors(issues));
    }
}

TEST_CASE("Presets are all at 2 m with sequential output channels") {
    for (const auto& id : layoutpresets::ids()) {
        INFO(id);
        const auto layout = *layoutpresets::load(id);
        int i = 0;
        for (const auto& s : layout.speakers) {
            REQUIRE(s.position.distance == 2.0f);
            REQUIRE(s.outputChannel == i);
            REQUIRE(s.id == i + 1);
            REQUIRE(s.position.azimuth > -180.0f);
            REQUIRE(s.position.azimuth <= 180.0f);
            ++i;
        }
    }
}

TEST_CASE("Standard angles (azimuth is clockwise-positive)") {
    const auto stereo = *layoutpresets::load("stereo");
    REQUIRE(stereo.speakers[0].position.azimuth == -30.0f);  // L
    REQUIRE(stereo.speakers[1].position.azimuth == 30.0f);   // R

    const auto ring = *layoutpresets::load("ring16");
    for (std::size_t i = 0; i < ring.speakers.size(); ++i) {
        const float expected = Position::wrapAzimuth(22.5f * static_cast<float>(i));
        REQUIRE_THAT(ring.speakers[i].position.azimuth, Catch::Matchers::WithinAbs(expected, 1e-4));
        REQUIRE(ring.speakers[i].position.elevation == 0.0f);
    }

    const auto atmos = *layoutpresets::load("7.1.4");
    for (std::size_t i = 8; i < 12; ++i) REQUIRE(atmos.speakers[i].position.elevation > 0.0f);

    const auto dome = *layoutpresets::load("dome22");
    REQUIRE(dome.speakers.back().position.elevation == 90.0f);
}

TEST_CASE("Presets are usable as a full-size layout (compensation is zero at equal distance)") {
    const auto layout = *layoutpresets::load("dome22");
    for (const auto& c : computeCompensation(layout)) {
        REQUIRE_THAT(c.delayMs, Catch::Matchers::WithinAbs(0.0, 1e-6));
        REQUIRE_THAT(c.gainDb, Catch::Matchers::WithinAbs(0.0, 1e-6));
    }
}

TEST_CASE("Exactly the LFE speakers of 5.1, 7.1 and 7.1.4 are marked lfe") {
    for (const auto& id : layoutpresets::ids()) {
        INFO(id);
        const auto layout = *layoutpresets::load(id);
        int lfeCount = 0;
        for (const auto& s : layout.speakers) {
            REQUIRE(s.lfe == (s.name == "LFE"));
            lfeCount += s.lfe ? 1 : 0;
        }
        const bool surround = id == "5.1" || id == "7.1" || id == "7.1.4";
        REQUIRE(lfeCount == (surround ? 1 : 0));
    }
}
