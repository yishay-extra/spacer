#pragma once

#include <cstdint>

#include "core/Clock.h"
#include "core/clock/ClockTimeline.h"

namespace spacer {

/// Turns an external ClockTimeline (host time) into the per-block ClockState
/// of the ClockSource contract (SPEC §9): the state at the START of the
/// block, with `startOffset` / `startBeat` when the transport starts inside
/// it.
///
///  - While playing, the reported beat advances by exactly the block's
///    samples at the reference tempo, plus a fraction (kCorrection) of the
///    phase error against the reference: no per-block jitter, and never
///    backwards. An error over kJumpSeconds (locate, relock) jumps.
///  - A start (stopped → playing) whose host time falls inside the block is
///    reported at that sample; one already in the past (events arrive with
///    some latency) at sample 0, with startBeat the reference beat there.
/// Audio thread; no allocation.
class ClockFollower {
public:
    static constexpr double kCorrection = 0.2;
    static constexpr double kJumpSeconds = 0.05;

    void reset();

    /// `blockTime`: host time of the block's first sample (BlockTimer);
    /// `secondsPerSample`: host seconds per sample.
    ClockState follow(const ClockTimeline& timeline, double blockTime, int numSamples, double sampleRate,
                      double secondsPerSample, int beatsPerBar);

private:
    double beat_{0.0};  // predicted beat at the start of the next block
    double lastReported_{0.0};
    double bpm_{120.0};
    bool playing_{false};
    bool havePrediction_{false};
    std::uint32_t epoch_{0};
};

}  // namespace spacer
