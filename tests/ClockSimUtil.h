#pragma once

// Synthetic sender / audio-thread simulation shared by the external clock
// tests: jittered event timestamps, jittered callback times, error in ms.

#include <cmath>
#include <cstdint>
#include <functional>
#include <random>

#include "core/Clock.h"
#include "core/clock/BlockTimer.h"
#include "core/clock/ClockFollower.h"
#include "core/clock/ClockTimeline.h"

namespace spacer::testing {

/// Uniform jitter in [-amount, +amount].
class Jitter {
public:
    explicit Jitter(double amount, std::uint32_t seed = 1) : amount_(amount), rng_(seed) {}
    double operator()() {
        return amount_ > 0.0 ? std::uniform_real_distribution<double>(-amount_, amount_)(rng_) : 0.0;
    }

private:
    double amount_;
    std::mt19937 rng_;
};

/// Simulated audio thread: blocks of `blockSize` at a true rate that may
/// differ from the nominal one by `ppm`, with callback time jitter. Each
/// block calls `feed(now)` (deliver due events to the tracker), then
/// `timeline()` and returns the follower's state together with the true
/// block start time.
struct AudioSim {
    double sampleRate{48000.0};
    int blockSize{128};
    double ppm{0.0};
    double callbackJitter{0.0005};
    double t0{100.0};
    std::uint32_t seed{7};

    struct Block {
        double trueTime;
        ClockState state;
    };

    template <typename Feed, typename GetTimeline, typename OnBlock>
    void run(double seconds, Feed&& feed, GetTimeline&& timeline, OnBlock&& onBlock, int beatsPerBar = 4) {
        BlockTimer timer;
        timer.prepare(sampleRate);
        ClockFollower follower;
        std::mt19937 rng(seed);
        std::uniform_real_distribution<double> jit(0.0, callbackJitter);
        const double truePeriod = 1.0 / (sampleRate * (1.0 + ppm * 1e-6));
        const auto blocks = static_cast<long long>(seconds * sampleRate / blockSize);
        for (long long k = 0; k < blocks; ++k) {
            const double trueTime = t0 + static_cast<double>(k * blockSize) * truePeriod;
            const double now = trueTime + (callbackJitter > 0.0 ? jit(rng) : 0.0);
            feed(now);
            const double blockTime = timer.blockStart(now, blockSize);
            const ClockTimeline& tl = timeline(now);
            const ClockState s =
                follower.follow(tl, blockTime, blockSize, sampleRate, timer.secondsPerSample(), beatsPerBar);
            onBlock(Block{trueTime, s});
        }
    }
};

}  // namespace spacer::testing
