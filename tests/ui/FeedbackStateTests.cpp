#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "ui/logic/FeedbackState.h"

using namespace spacer;
using Catch::Matchers::WithinAbs;

TEST_CASE("Speaker meters hold the peak and fall back over time", "[ui][feedback]") {
    FeedbackState f;
    REQUIRE(f.speakerLevelDb(1) == FeedbackState::kMeterFloorDb);
    f.apply(fb::SpeakerMeter{1, 0.5f, -2.0f});
    REQUIRE_THAT(f.speakerLevelDb(1), WithinAbs(-6.0206, 1e-3));
    REQUIRE(f.speakerGainReductionDb(1) == -2.0f);
    f.apply(fb::SpeakerMeter{1, 0.1f, 0.0f});  // lower peak: display holds
    REQUIRE_THAT(f.speakerLevelDb(1), WithinAbs(-6.0206, 1e-3));
    f.decay(0.5);
    REQUIRE_THAT(f.speakerLevelDb(1), WithinAbs(-6.0206 - 12.0, 1e-3));
    f.decay(100.0);
    REQUIRE(f.speakerLevelDb(1) == FeedbackState::kMeterFloorDb);
}

TEST_CASE("Source meters take the louder channel", "[ui][feedback]") {
    FeedbackState f;
    f.apply(fb::SourceMeter{5, {0.25f, 1.0f}});
    REQUIRE_THAT(f.sourceLevelDb(5), WithinAbs(0.0, 1e-4));
}

TEST_CASE("Rendered positions are stored per point and can be cleared", "[ui][feedback]") {
    FeedbackState f;
    REQUIRE_FALSE(f.sourcePosition(2, 0));
    f.apply(fb::SourcePosition{2, 1, {45, 0, 2}});
    REQUIRE_FALSE(f.sourcePosition(2, 0));
    REQUIRE(*f.sourcePosition(2, 1) == Position{45, 0, 2});
    f.clearPositions();
    REQUIRE_FALSE(f.sourcePosition(2, 1));
}

TEST_CASE("Clock, xruns and dropped commands are tracked", "[ui][feedback]") {
    FeedbackState f;
    REQUIRE_FALSE(f.hasClock());
    ClockState c;
    c.beat = 7.5;
    f.apply(fb::Clock{c});
    REQUIRE(f.hasClock());
    REQUIRE(f.clock().beat == 7.5);
    f.apply(fb::Xrun{3});
    f.apply(fb::CommandDropped{9});
    REQUIRE(f.xruns() == 3);
    REQUIRE(f.droppedCommands() == 9);
}

TEST_CASE("Meter fraction maps the dB scale to 0..1", "[ui][feedback]") {
    REQUIRE(FeedbackState::meterFraction(FeedbackState::kMeterFloorDb) == 0.0f);
    REQUIRE(FeedbackState::meterFraction(-100.0f) == 0.0f);
    REQUIRE(FeedbackState::meterFraction(0.0f) == 1.0f);
    REQUIRE(FeedbackState::meterFraction(6.0f) == 1.0f);
    REQUIRE_THAT(FeedbackState::meterFraction(-30.0f), WithinAbs(0.5, 1e-6));
    REQUIRE(FeedbackState::toDb(0.0f) == FeedbackState::kMeterFloorDb);
}
