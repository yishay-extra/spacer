#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/LayoutPresets.h"
#include "ui/logic/StageScene.h"

using namespace spacer;
using Catch::Matchers::WithinAbs;

namespace {

Project makeProject() {
    Project p;
    p.layout = *layoutpresets::load("quad");
    SourceSpec manual;
    manual.id = 1;
    manual.name = "Voice";
    manual.motion.value = ManualMotion{{0, 0, 1}, 20.0f};
    SourceSpec orbit;
    orbit.id = 2;
    orbit.name = "Pad";
    orbit.stereo = true;
    orbit.motion.value = OrbitMotion{};
    p.sources = {manual, orbit};
    return p;
}

}  // namespace

TEST_CASE("Stage scene lists speakers, source points and trajectories", "[ui][scene]") {
    const Project p = makeProject();
    StageScene scene;
    scene.setProject(p);
    FeedbackState f;
    f.apply(fb::SpeakerMeter{p.layout.speakers[0].id, 1.0f, 0.0f});
    scene.update(p, f);

    REQUIRE(scene.speakers().size() == 4);
    REQUIRE(scene.speakers()[0].levelDb == 0.0f);
    REQUIRE(scene.sources().size() == 3);  // mono + stereo pair
    REQUIRE(scene.sources()[0].name == "Voice");
    REQUIRE(scene.sources()[1].channel == 0);
    REQUIRE(scene.sources()[2].channel == 1);
    // Only the orbit has a trajectory (one path for a non-linked stereo source).
    REQUIRE(scene.trajectories().size() == 1);
    REQUIRE(scene.trajectories()[0].sourceId == 2);
    REQUIRE(scene.items().size() == 7);
}

TEST_CASE("Rendered positions replace model positions", "[ui][scene]") {
    const Project p = makeProject();
    StageScene scene;
    FeedbackState f;
    scene.update(p, f);
    REQUIRE_THAT(scene.sources()[0].position.y, WithinAbs(1.0, 1e-5));
    f.apply(fb::SourcePosition{1, 0, {90, 0, 2}});
    scene.update(p, f);
    REQUIRE_THAT(scene.sources()[0].position.x, WithinAbs(2.0, 1e-5));
}
