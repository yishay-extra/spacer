#include <catch2/catch_test_macros.hpp>

#include "core/ProjectFile.h"
#include "core/Validate.h"
#include "ui/logic/StageScene.h"

using namespace spacer;

// resources/examples/stage-stress.spacer is the project the WP15 Mac checklist
// (docs/mac-validation/WP15.md, check 10) uses for the paint-time measurement.
TEST_CASE("The stage stress project loads, validates and is at the limits", "[ui][demo]") {
    const auto loaded =
        projectfile::load(std::string(SPACER_SOURCE_DIR) + "/resources/examples/stage-stress.spacer");
    REQUIRE(loaded);
    REQUIRE(loaded->unknownKeys.empty());
    const Project& p = loaded->project;
    REQUIRE_FALSE(hasErrors(validate(p)));
    REQUIRE(p.layout.speakers.size() == 32);
    REQUIRE(p.sources.size() == 64);
    for (const auto& sp : p.layout.speakers) {
        REQUIRE(sp.outputChannel == -1);
        REQUIRE(sp.trimDb == -60.0f);
    }
    for (const auto& s : p.sources) {
        REQUIRE(s.spatializer == SpatializerKind::Dbap);
        REQUIRE(s.spread == 1.0f);
        REQUIRE_FALSE(s.mute);
    }
    // Every source loud: every (point, speaker) pair carries a share.
    FeedbackState f;
    for (const auto& s : p.sources) f.apply(fb::SourceMeter{s.id, {1.0f, 0.0f}});
    StageScene scene;
    scene.setProject(p);
    scene.update(p, f);
    REQUIRE(scene.loudPoints() == 64);
    REQUIRE(scene.sharePairs() > 64 * 16);
}
