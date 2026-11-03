#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <cmath>

#include "ClockSimUtil.h"
#include "core/clock/OscBeatTracker.h"

using namespace spacer;
using Catch::Approx;

TEST_CASE("OscBeatTracker: advancing beats start the transport", "[clock][osc]") {
    OscBeatTracker t(120.0);
    t.beat(4.0, 1.0);
    CHECK_FALSE(t.timeline().playing);
    CHECK(t.timeline().anchorBeat == 4.0);
    t.beat(4.1, 1.05);
    CHECK(t.timeline().playing);
    CHECK(t.timeline().startBeat == Approx(4.1));
    CHECK(t.timeline().bpm() == Approx(120.0));
}

TEST_CASE("OscBeatTracker: estimates tempo from jittered messages", "[clock][osc]") {
    const double rateHz = GENERATE(10.0, 30.0, 60.0);
    CAPTURE(rateHz);
    OscBeatTracker tracker(100.0);  // configured tempo is wrong on purpose
    const double bpm = 133.0;
    testing::Jitter jitter(0.002, 9);  // +-2 ms network / scheduling jitter
    testing::AudioSim sim;
    const double start = 100.2;
    double nextMsg = start;
    double maxErrMs = 0.0;
    sim.run(
        20.0,
        [&](double now) {
            while (nextMsg <= now) {
                // The receiver timestamps on arrival; the beat is the sender's at send time.
                tracker.beat((nextMsg - start) * bpm / 60.0, nextMsg + 0.001 + jitter());
                nextMsg += 1.0 / rateHz;
            }
            tracker.update(now);
        },
        [&](double) -> const ClockTimeline& { return tracker.timeline(); },
        [&](const testing::AudioSim::Block& b) {
            if (b.trueTime - start < 4.0) return;
            const double truth = (b.trueTime - start) * bpm / 60.0;
            maxErrMs = std::max(maxErrMs, std::abs(b.state.beat - truth) / (bpm / 60.0) * 1000.0);
            CHECK(b.state.bpm == Approx(bpm).epsilon(0.01));
        });
    CHECK(maxErrMs < 3.0);
    CHECK(tracker.timeline().locked);
}

TEST_CASE("OscBeatTracker: repeated beat or timeout stops, jump locates", "[clock][osc]") {
    OscBeatTracker t(120.0);
    for (int i = 0; i < 20; ++i) t.beat(i * 0.1, 1.0 + i * 0.05);
    CHECK(t.timeline().playing);
    const auto epoch = t.timeline().epoch;
    t.beat(32.0, 2.0);  // locate
    CHECK(t.timeline().epoch == epoch + 1);
    CHECK(t.timeline().beatAt(2.0) == Approx(32.0));
    t.beat(32.0, 2.05);  // same beat again: stopped
    CHECK_FALSE(t.timeline().playing);
    t.beat(32.1, 2.1);
    CHECK(t.timeline().playing);
    t.update(3.5);
    CHECK_FALSE(t.timeline().playing);
    CHECK(t.timeline().beatAt(10.0) == Approx(t.timeline().beatAt(3.5)));
}
