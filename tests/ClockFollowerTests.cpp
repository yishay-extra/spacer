#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/clock/BlockTimer.h"
#include "core/clock/ClockFollower.h"

using namespace spacer;
using Catch::Approx;

namespace {
constexpr double kSr = 48000.0;
constexpr double kSps = 1.0 / kSr;

ClockTimeline playingFrom(double time, double beat, double bps = 2.0) {
    ClockTimeline tl;
    tl.valid = true;
    tl.playing = true;
    tl.anchorTime = time;
    tl.anchorBeat = beat;
    tl.beatsPerSecond = bps;
    tl.startTime = time;
    tl.startBeat = beat;
    tl.epoch = 1;
    tl.maxAheadSeconds = 100.0;
    return tl;
}
}  // namespace

TEST_CASE("ClockFollower: start inside the block is reported at its sample", "[clock][follower]") {
    ClockFollower f;
    ClockTimeline stopped;
    stopped.valid = true;
    stopped.anchorBeat = 8.0;
    auto s = f.follow(stopped, 0.0, 128, kSr, kSps, 4);
    CHECK_FALSE(s.playing);
    CHECK(s.startOffset == -1);
    CHECK(s.beat == 8.0);

    const double blockTime = 128 * kSps;
    const auto tl = playingFrom(blockTime + 50 * kSps, 8.0);
    s = f.follow(tl, blockTime, 128, kSr, kSps, 4);
    CHECK(s.startOffset == 50);
    CHECK(s.startBeat == Approx(8.0));
    CHECK_FALSE(s.playing);
    s = f.follow(tl, blockTime + 128 * kSps, 128, kSr, kSps, 4);
    CHECK(s.playing);
    CHECK(s.startOffset == -1);
    CHECK(s.beat == Approx(8.0 + 78 * kSps * 2.0));
}

TEST_CASE("ClockFollower: a start in the past is reported at sample 0", "[clock][follower]") {
    ClockFollower f;
    const auto tl = playingFrom(1.0, 0.0);
    const auto s = f.follow(tl, 1.002, 128, kSr, kSps, 4);
    CHECK(s.startOffset == 0);
    CHECK(s.playing);
    CHECK(s.startBeat == Approx(0.004));
    CHECK(s.beat == Approx(0.004));
}

TEST_CASE("ClockFollower: a start beyond the block waits", "[clock][follower]") {
    ClockFollower f;
    const auto tl = playingFrom(1.0, 0.0);
    const auto s = f.follow(tl, 0.9, 128, kSr, kSps, 4);
    CHECK(s.startOffset == -1);
    CHECK_FALSE(s.playing);
    CHECK(f.follow(tl, 1.0 - 10 * kSps, 128, kSr, kSps, 4).startOffset == 10);
}

TEST_CASE("ClockFollower: smooth, monotonic and jumps on a locate", "[clock][follower]") {
    ClockFollower f;
    auto tl = playingFrom(0.0, 0.0);
    double t = 0.0, prev = -1.0;
    for (int i = 0; i < 1000; ++i, t += 128 * kSps) {
        // Reference wobbles by +-0.2 ms around the truth.
        tl.anchorTime = (i % 2 ? 0.0002 : -0.0002);
        const auto s = f.follow(tl, t, 128, kSr, kSps, 4);
        CHECK(s.beat >= prev);
        prev = s.beat;
        if (i > 50) CHECK(s.beat == Approx(t * 2.0).margin(0.0005));
    }
    tl.anchorBeat = 100.0;
    tl.anchorTime = t;
    ++tl.epoch;
    const auto s = f.follow(tl, t, 128, kSr, kSps, 4);
    CHECK(s.beat == Approx(100.0));
    CHECK(s.startOffset == -1);
}

TEST_CASE("BlockTimer: filters callback jitter and restarts after a stall", "[clock][follower]") {
    BlockTimer timer;
    timer.prepare(kSr);
    double maxErr = 0.0;
    for (int i = 0; i < 20000; ++i) {
        const double truth = 10.0 + i * 128 * kSps;
        const double jitter = ((i * 7919) % 11) * 0.0001;  // 0..1 ms, deterministic
        const double t = timer.blockStart(truth + jitter, 128);
        if (i > 2000) maxErr = std::max(maxErr, std::abs(t - truth - 0.0005));
    }
    CHECK(maxErr < 0.0003);
    // A 1 s stall: the filter restarts at the reading.
    const double after = 10.0 + 20000 * 128 * kSps + 1.0;
    CHECK(timer.blockStart(after, 128) == after);
}
