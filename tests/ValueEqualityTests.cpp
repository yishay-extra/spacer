#include <catch2/catch_test_macros.hpp>

#include "core/Cue.h"
#include "core/MotionSpec.h"

using namespace spacer;

TEST_CASE("Period equality covers every member") {
    const Period a = Period::beats(4.0);
    CHECK(a == Period::beats(4.0));
    CHECK(a != Period::seconds(4.0));
    CHECK(a != Period::beats(5.0));
}

TEST_CASE("trigger::AtBeat equality covers every member") {
    const trigger::AtBeat a{2, 3.0};
    CHECK(a == trigger::AtBeat{2, 3.0});
    CHECK(a != trigger::AtBeat{3, 3.0});
    CHECK(a != trigger::AtBeat{2, 4.0});
}

TEST_CASE("trigger::AfterPrevious equality covers every member") {
    const trigger::AfterPrevious a{Period::beats(2.0), 1.0};
    CHECK(a == trigger::AfterPrevious{Period::beats(2.0), 1.0});
    CHECK(a != trigger::AfterPrevious{Period::seconds(2.0), 1.0});
    CHECK(a != trigger::AfterPrevious{Period::beats(3.0), 1.0});
    CHECK(a != trigger::AfterPrevious{Period::beats(2.0), 2.0});
}

TEST_CASE("Cue equality covers every member") {
    Cue a;
    a.name = "a";
    a.sceneName = "s";
    a.transitionSeconds = 1.0f;
    a.trigger.value = trigger::AtBeat{2, 3.0};
    a.restartMotionPhase = true;
    a.postWaitSeconds = 2.0f;

    const Cue same = a;
    CHECK(a == same);

    Cue c = a;
    c.name = "b";
    CHECK(a != c);
    c = a;
    c.sceneName = "t";
    CHECK(a != c);
    c = a;
    c.transitionSeconds = 1.5f;
    CHECK(a != c);
    c = a;
    c.trigger.value = trigger::AtBeat{2, 4.0};
    CHECK(a != c);
    c = a;
    c.restartMotionPhase = false;
    CHECK(a != c);
    c = a;
    c.postWaitSeconds = 2.5f;
    CHECK(a != c);
}
