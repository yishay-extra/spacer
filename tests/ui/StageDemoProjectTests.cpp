#include <catch2/catch_test_macros.hpp>

#include "core/ProjectFile.h"
#include "core/Validate.h"
#include "ui/logic/StageScene.h"

using namespace spacer;

// resources/examples/stage-demo.spacer is the project the WP11a Mac checklist
// (docs/mac-validation/WP11a.md) uses.
TEST_CASE("The stage demo project loads, validates and has something to draw", "[ui][demo]") {
    const auto loaded =
        projectfile::load(std::string(SPACER_SOURCE_DIR) + "/resources/examples/stage-demo.spacer");
    REQUIRE(loaded);
    REQUIRE(loaded->unknownKeys.empty());
    const Project& p = loaded->project;
    REQUIRE_FALSE(hasErrors(validate(p)));
    StageScene scene;
    scene.setProject(p);
    scene.update(p, FeedbackState{});
    REQUIRE(scene.speakers().size() == 4);
    REQUIRE(scene.sources().size() == 4);       // Voice, Pad, Pair L/R
    REQUIRE(scene.trajectories().size() == 1);  // Pad orbits
}
