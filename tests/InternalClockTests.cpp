#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

#include "core/InternalClock.h"
#include "core/Project.h"

using Catch::Approx;
using namespace spacer;

namespace {

InternalClock makeClock(double bpm = 120.0, double sampleRate = 48000.0) {
    InternalClock clock(sampleRate, 4);
    clock.setBpm(bpm);
    return clock;
}

}  // namespace

TEST_CASE("Advancing one second at 120 bpm adds exactly two beats") {
    auto clock = makeClock(120.0, 48000.0);
    clock.resume();
    clock.advance(48000);
    const ClockState after = clock.advance(0);
    CHECK(after.beat == Approx(2.0));
    CHECK(after.timeSeconds == Approx(1.0));
    CHECK(after.playing);
    CHECK(after.bpm == Approx(120.0));
}

TEST_CASE("Beat advance does not depend on block size") {
    auto clock = makeClock(120.0, 48000.0);
    clock.resume();
    for (int i = 0; i < 100; ++i) clock.advance(480);  // 100 x 480 = 48000
    CHECK(clock.advance(0).beat == Approx(2.0));
}

TEST_CASE("Beat advance follows the sample rate") {
    auto clock = makeClock(120.0, 44100.0);
    clock.resume();
    clock.advance(44100);
    CHECK(clock.advance(0).beat == Approx(2.0));
}

TEST_CASE("Stop freezes the beat but time keeps running") {
    auto clock = makeClock(120.0);
    clock.play();
    clock.advance(24000);  // half a second, one beat
    clock.stop();
    clock.advance(48000);
    const ClockState s = clock.advance(0);
    CHECK(s.beat == Approx(1.0));
    CHECK(s.timeSeconds == Approx(1.5));
    CHECK_FALSE(s.playing);
}

TEST_CASE("Resume continues from the frozen beat") {
    auto clock = makeClock(120.0);
    clock.play();
    clock.advance(24000);
    clock.stop();
    clock.advance(48000);
    clock.resume();
    clock.advance(24000);
    CHECK(clock.advance(0).beat == Approx(2.0));
}

TEST_CASE("Tempo changes take effect from the current beat") {
    auto clock = makeClock(60.0);
    clock.play();
    clock.advance(48000);  // one beat at 60 bpm
    clock.setBpm(240.0);
    clock.advance(48000);  // four beats at 240 bpm
    CHECK(clock.advance(0).beat == Approx(5.0));
}

TEST_CASE("Tempo is clamped to 20..300 bpm") {
    auto clock = makeClock();
    clock.setBpm(1000.0);
    CHECK(clock.advance(0).bpm == Approx(300.0));
    clock.setBpm(1.0);
    CHECK(clock.advance(0).bpm == Approx(20.0));
    clock.setBpm(NAN);
    CHECK(clock.advance(0).bpm == Approx(20.0));
}

TEST_CASE("setBeat repositions the transport") {
    auto clock = makeClock(120.0);
    clock.setBeat(8.0);
    clock.play();
    clock.advance(48000);
    CHECK(clock.advance(0).beat == Approx(10.0));
}

TEST_CASE("Zero-length blocks change nothing") {
    auto clock = makeClock(120.0);
    clock.play();
    clock.advance(0);
    clock.advance(0);
    const ClockState s = clock.advance(0);
    CHECK(s.beat == Approx(0.0));
    CHECK(s.timeSeconds == Approx(0.0));
}

TEST_CASE("current() returns the state at the start of the last block") {
    auto clock = makeClock(120.0);
    clock.play();
    const ClockState start = clock.advance(48000);
    CHECK(start.beat == Approx(0.0));
    CHECK(clock.current().beat == Approx(0.0));
    CHECK(clock.advance(0).beat == Approx(2.0));
    CHECK(clock.current().beat == Approx(2.0));
}

TEST_CASE("Quantised start from a mid-bar beat waits for the next bar") {
    auto clock = makeClock(120.0);
    clock.setQuantisedStart(true);
    clock.setBeat(1.0);
    clock.play();
    CHECK(clock.armed());

    // Next boundary is beat 4: three beats = 72000 samples away.
    clock.advance(71999);
    CHECK_FALSE(clock.advance(0).playing);
    CHECK(clock.advance(0).beat == Approx(1.0));  // beat frozen while armed

    // This block crosses the boundary, one sample into the next beat, so the
    // transport starts within this block.
    clock.advance(48000);
    CHECK(clock.armed() == false);
    const ClockState s = clock.advance(0);
    CHECK(s.playing);
    CHECK(s.beat == Approx(4.0 + 2.0 * 47999.0 / 48000.0));
}

TEST_CASE("Quantised start on a bar boundary starts within the next block") {
    auto clock = makeClock(120.0);
    clock.setQuantisedStart(true);
    clock.play();  // beat 0 is a bar boundary
    clock.advance(512);
    const ClockState s = clock.advance(0);
    CHECK(s.playing);
    CHECK(s.beat == Approx(512.0 * 120.0 / (60.0 * 48000.0)));
}

TEST_CASE("Quantised start that lands exactly on a block edge") {
    auto clock = makeClock(120.0);
    clock.setQuantisedStart(true);
    clock.setBeat(3.0);
    clock.play();
    clock.advance(24000);  // 1 beat to go = 24000 samples, exactly this block
    CHECK_FALSE(clock.armed());
    CHECK(clock.advance(0).beat == Approx(4.0));
}

TEST_CASE("Quantised start counts down across many blocks") {
    auto clock = makeClock(120.0);
    clock.setQuantisedStart(true);
    clock.setBeat(3.0);
    clock.play();
    for (int i = 0; i < 23; ++i) clock.advance(1024);  // 23552 samples
    CHECK(clock.armed());
    CHECK(clock.advance(0).beat == Approx(3.0));
    clock.advance(1024);  // 24576 total: 576 samples past the boundary
    CHECK_FALSE(clock.armed());
    CHECK(clock.advance(0).beat == Approx(4.0 + 576.0 * 2.0 / 48000.0));
}

TEST_CASE("Quantised start honours a tempo change while armed") {
    auto clock = makeClock(120.0);
    clock.setQuantisedStart(true);
    clock.setBeat(3.0);
    clock.play();
    clock.advance(12000);  // 0.5 beat at 120 bpm; 0.5 beat left
    clock.setBpm(60.0);    // now 0.5 beat is 0.5 s = 24000 samples
    clock.advance(23999);
    CHECK(clock.armed());
    clock.advance(2);
    CHECK_FALSE(clock.armed());
}

TEST_CASE("Quantised start is off by default") {
    auto clock = makeClock(120.0);
    clock.setBeat(1.5);
    clock.play();
    CHECK_FALSE(clock.armed());
    CHECK(clock.advance(0).playing);
}

TEST_CASE("resume() ignores quantisation") {
    auto clock = makeClock(120.0);
    clock.setQuantisedStart(true);
    clock.setBeat(1.0);
    clock.resume();
    CHECK(clock.advance(0).playing);
    CHECK_FALSE(clock.armed());
}

TEST_CASE("Stop cancels a pending quantised start") {
    auto clock = makeClock(120.0);
    clock.setQuantisedStart(true);
    clock.setBeat(1.0);
    clock.play();
    clock.stop();
    CHECK_FALSE(clock.armed());
    clock.advance(200000);
    const ClockState s = clock.advance(0);
    CHECK_FALSE(s.playing);
    CHECK(s.beat == Approx(1.0));
}

TEST_CASE("setBeat re-targets a pending quantised start") {
    auto clock = makeClock(120.0);
    clock.setQuantisedStart(true);
    clock.setBeat(1.0);
    clock.play();
    clock.setBeat(5.5);    // next boundary is now 8
    clock.advance(48000);  // 2.5 beats to go = 60000 samples: still waiting
    CHECK(clock.armed());
    CHECK(clock.advance(0).beat == Approx(5.5));
}

TEST_CASE("play() on a playing clock is a no-op") {
    auto clock = makeClock(120.0);
    clock.play();
    clock.advance(48000);
    clock.play();
    CHECK(clock.advance(0).beat == Approx(2.0));
}

TEST_CASE("Tap tempo: steady taps at 0.5 s give 120 bpm") {
    auto clock = makeClock(90.0);
    clock.tap(0.0);
    clock.tap(0.5);
    clock.tap(1.0);
    clock.tap(1.5);
    CHECK(clock.advance(0).bpm == Approx(120.0));
}

TEST_CASE("Tap tempo: a single tap leaves the tempo alone") {
    auto clock = makeClock(90.0);
    clock.tap(3.0);
    CHECK(clock.advance(0).bpm == Approx(90.0));
}

TEST_CASE("Tap tempo: intervals are averaged") {
    auto clock = makeClock(90.0);
    clock.tap(0.0);
    clock.tap(0.6);  // 100 bpm
    clock.tap(1.0);  // 0.4 s
    // mean of 0.6 and 0.4 = 0.5 s
    CHECK(clock.advance(0).bpm == Approx(120.0));
}

TEST_CASE("Tap tempo: a gap longer than the timeout starts a new series") {
    auto clock = makeClock(90.0);
    clock.tap(0.0);
    clock.tap(0.5);   // 120 bpm
    clock.tap(10.0);  // too late: restarts the series, tempo unchanged
    CHECK(clock.advance(0).bpm == Approx(120.0));
    clock.tap(10.6);  // 100 bpm
    CHECK(clock.advance(0).bpm == Approx(100.0));
}

TEST_CASE("Tap tempo is clamped to the tempo range") {
    auto clock = makeClock(90.0);
    clock.tap(0.0);
    clock.tap(0.1);  // 600 bpm
    CHECK(clock.advance(0).bpm == Approx(300.0));
}

TEST_CASE("Time since start is monotonic across stop and play") {
    auto clock = makeClock(120.0);
    clock.advance(1000);
    clock.play();
    clock.advance(1000);
    clock.stop();
    clock.advance(1000);
    CHECK(clock.advance(0).timeSeconds == Approx(3000.0 / 48000.0));
}

TEST_CASE("configure() applies the project's clock settings") {
    ClockConfig cfg;
    cfg.bpm = 96.0;
    cfg.beatsPerBar = 3;
    cfg.quantisedStart = true;

    InternalClock clock;
    clock.configure(cfg);
    CHECK(clock.advance(0).bpm == Approx(96.0));
    CHECK(clock.advance(0).beatsPerBar == 3);

    clock.setBeat(0.5);
    clock.play();
    CHECK(clock.armed());  // quantised start was applied
}

TEST_CASE("Beats per bar is at least one") {
    InternalClock clock;
    clock.setBeatsPerBar(0);
    CHECK(clock.advance(0).beatsPerBar == 1);
}
