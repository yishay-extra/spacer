#include <cmath>

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

TEST_CASE("Source points carry colour, level and shares", "[ui][scene]") {
    const Project p = makeProject();  // quad; source 1 is Manual at az 0, 1 m: between FL and FR
    StageScene scene;
    FeedbackState f;
    scene.update(p, f);
    REQUIRE(scene.sources()[0].colour == sourceColour(1));
    REQUIRE(scene.sources()[1].colour == sourceColour(2));
    REQUIRE(scene.sources()[0].level == 0.0f);  // silent: a dot only
    REQUIRE_FALSE(scene.sources()[0].hasShares);
    REQUIRE(scene.loudPoints() == 0);

    f.apply(fb::SourceMeter{1, {1.0f, 0.0f}});  // 0 dBFS
    scene.update(p, f);
    const auto& s = scene.sources()[0];
    REQUIRE(s.level == 1.0f);
    REQUIRE(s.hasShares);
    REQUIRE(scene.loudPoints() == 1);
    for (std::size_t i = 0; i < 4; ++i) {
        const bool front = std::abs(p.layout.speakers[i].position.azimuth) < 90.0f;
        REQUIRE_THAT(s.shares[i], WithinAbs(front ? 0.70711 : 0.0, 1e-3));
    }
    for (std::size_t i = 4; i < s.shares.size(); ++i) REQUIRE(s.shares[i] == 0.0f);
}

TEST_CASE("Shares scale with level and tiny shares are dropped", "[ui][scene]") {
    const Project p = makeProject();
    StageScene scene;
    FeedbackState half;
    half.apply(fb::SourceMeter{1, {0.0316228f, 0.0f}});  // -30 dBFS: fraction 0.5
    scene.update(p, half);
    REQUIRE_THAT(scene.sources()[0].level, WithinAbs(0.5, 1e-3));
    REQUIRE_THAT(scene.sources()[0].shares[0], WithinAbs(0.35355, 1e-3));
    FeedbackState quiet;
    quiet.apply(fb::SourceMeter{1, {0.0013183f, 0.0f}});  // -57.6 dBFS: fraction 0.04, share 0.028
    scene.update(p, quiet);
    REQUIRE(scene.sources()[0].level > 0.0f);
    REQUIRE(scene.loudPoints() == 1);
    for (float share : scene.sources()[0].shares) REQUIRE(share == 0.0f);
}

TEST_CASE("Muted and ambisonic sources have no shares", "[ui][scene]") {
    Project p = makeProject();
    FeedbackState f;
    f.apply(fb::SourceMeter{1, {1.0f, 0.0f}});
    StageScene scene;
    p.sources[0].mute = true;
    scene.update(p, f);
    REQUIRE(scene.sources()[0].level == 0.0f);
    REQUIRE_FALSE(scene.sources()[0].hasShares);
    REQUIRE(scene.loudPoints() == 0);
    p.sources[0].mute = false;
    p.sources[0].spatializer = SpatializerKind::Ambisonic;
    scene.update(p, f);
    REQUIRE(scene.sources()[0].level == 1.0f);
    REQUIRE_FALSE(scene.sources()[0].hasShares);
    for (float share : scene.sources()[0].shares) REQUIRE(share == 0.0f);
}

TEST_CASE("Shares follow a layout change and stereo points share one level", "[ui][scene]") {
    Project p = makeProject();
    FeedbackState f;
    f.apply(fb::SourceMeter{1, {1.0f, 0.0f}});
    f.apply(fb::SourceMeter{2, {1.0f, 0.2f}});
    StageScene scene;
    scene.update(p, f);
    REQUIRE(scene.sources()[1].level == scene.sources()[2].level);
    REQUIRE(scene.loudPoints() == 3);
    p.layout = *layoutpresets::load("dome22");
    scene.update(p, f);
    double sumSq = 0.0;
    for (float share : scene.sources()[0].shares) sumSq += static_cast<double>(share) * share;
    REQUIRE(sumSq > 0.99);
    REQUIRE(sumSq < 1.001);
}
