#include <catch2/catch_test_macros.hpp>

#include "core/LayoutPresets.h"
#include "core/Validate.h"
#include "ui/logic/SpeakerTest.h"

using namespace spacer;

namespace {

Project makeProject(int numSources) {
    Project p;
    p.layout = *layoutpresets::load("quad");
    for (int i = 0; i < numSources; ++i) {
        SourceSpec s;
        s.id = i + 1;
        s.name = "S" + std::to_string(i + 1);
        p.sources.push_back(s);
    }
    return p;
}

}  // namespace

TEST_CASE("Speaker test solos pink noise on one speaker", "[ui][speakertest]") {
    const Project p = makeProject(3);
    const int speaker = p.layout.speakers[2].id;
    const Project t = speakertest::speakerTestProject(p, speaker);
    REQUIRE(t.sources.size() == 4);
    for (std::size_t i = 0; i < 3; ++i) REQUIRE(t.sources[i].mute);
    const SourceSpec& noise = t.sources.back();
    REQUIRE(noise.id == speakertest::testSourceId(p));
    REQUIRE(noise.id == 4);
    REQUIRE_FALSE(noise.mute);
    REQUIRE(noise.spatializer == SpatializerKind::Direct);
    REQUIRE(noise.directSpeakers == std::vector<int>{speaker});
    REQUIRE(std::get<GeneratorSpec>(noise.input.value).type == GeneratorKind::Pink);
    REQUIRE(noise.gainDb == speakertest::kNoiseGainDb);
    REQUIRE_FALSE(hasErrors(validate(t)));
    // Layout and everything else are untouched.
    REQUIRE(t.layout == p.layout);
}

TEST_CASE("Speaker test of an unknown speaker changes nothing", "[ui][speakertest]") {
    const Project p = makeProject(2);
    REQUIRE(speakertest::speakerTestProject(p, 999) == p);
}

TEST_CASE("Speaker test stays within the source limit", "[ui][speakertest]") {
    const Project p = makeProject(kMaxSources);
    const Project t = speakertest::speakerTestProject(p, p.layout.speakers[0].id);
    REQUIRE(t.sources.size() == static_cast<std::size_t>(kMaxSources));
    REQUIRE(t.sources.back().spatializer == SpatializerKind::Direct);
    REQUIRE_FALSE(hasErrors(validate(t)));
}
