#pragma once

// Helpers shared by the motion and modulator tests.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/InternalClock.h"
#include "core/Motion.h"
#include "core/Position.h"

namespace spacer::test {

inline constexpr double kRate = 48000.0;
inline constexpr int kBlock = 480;  // 10 ms

/// A playing InternalClock that hands out one MotionContext per block.
struct ClockDriver {
    InternalClock clock{kRate};
    int block{kBlock};

    explicit ClockDriver(double bpm = 120.0, bool playing = true, int blockSize = kBlock) : block(blockSize) {
        clock.setBpm(bpm);
        if (playing) clock.play();
    }
    MotionContext next() { return MotionContext{clock.advance(block), block, kRate}; }
};

/// Context built by hand: state at the START of a block of n samples.
inline MotionContext context(double beat, double timeSeconds, bool playing = true, double bpm = 120.0,
                             int n = kBlock) {
    ClockState s;
    s.bpm = bpm;
    s.beat = beat;
    s.playing = playing;
    s.sampleRate = kRate;
    s.timeSeconds = timeSeconds;
    return MotionContext{s, n, kRate};
}

/// Evaluate `blocks` blocks from `driver`, return the last position.
inline Position run(Motion& m, ClockDriver& driver, int blocks) {
    Position p = m.current();
    for (int i = 0; i < blocks; ++i) p = m.evaluate(driver.next());
    return p;
}

inline void requireNear(const Position& p, const Cartesian& expected, float tol = 1e-3f) {
    const Cartesian c = p.toCartesian();
    INFO("got (" << c.x << ", " << c.y << ", " << c.z << ") expected (" << expected.x << ", " << expected.y
                 << ", " << expected.z << ")");
    REQUIRE((c - expected).length() <= tol);
}

inline void requireNear(const Position& p, const Position& expected, float tol = 1e-3f) {
    requireNear(p, expected.toCartesian(), tol);
}

}  // namespace spacer::test
