#include "core/clock/ClockFollower.h"

#include <algorithm>
#include <cmath>

namespace spacer {

void ClockFollower::reset() {
    havePrediction_ = false;
    playing_ = false;
}

ClockState ClockFollower::follow(const ClockTimeline& tl, double blockTime, int numSamples, double sampleRate,
                                 double secondsPerSample, int beatsPerBar) {
    ClockState s;
    s.sampleRate = sampleRate;
    s.beatsPerBar = std::max(1, beatsPerBar);
    s.startOffset = -1;
    if (tl.valid) bpm_ = tl.bpm();
    s.bpm = bpm_;
    const int n = std::max(0, numSamples);
    const double bps = tl.beatsPerSecond;
    const double beatsPerSample = bps * secondsPerSample;

    if (!tl.playing || !tl.valid) {
        // Stopped: frozen at the reference beat.
        playing_ = false;
        beat_ = tl.valid ? tl.beatAt(blockTime) : beat_;
        havePrediction_ = tl.valid;
        epoch_ = tl.epoch;
        s.beat = beat_;
        s.playing = false;
        lastReported_ = s.beat;
        return s;
    }

    if (!playing_) {
        // Transport starts: where in this block?
        const double blockEnd = blockTime + n * secondsPerSample;
        if (tl.startTime >= blockEnd && n > 0) {
            // Start is still ahead (not normally the case: events are past).
            s.beat = tl.startBeat;
            s.playing = false;
            lastReported_ = s.beat;
            return s;
        }
        int offset = 0;
        if (tl.startTime > blockTime && n > 0)
            offset = std::clamp(static_cast<int>(std::lround((tl.startTime - blockTime) / secondsPerSample)),
                                0, n - 1);
        const double startBeat = tl.beatAt(blockTime + offset * secondsPerSample);
        s.startOffset = offset;
        s.startBeat = startBeat;
        if (offset == 0) {
            s.beat = startBeat;
            s.playing = true;
        } else {
            s.beat = havePrediction_ ? beat_ : startBeat;
            s.playing = false;
        }
        playing_ = true;
        havePrediction_ = true;
        epoch_ = tl.epoch;
        lastReported_ = s.beat;
        beat_ = startBeat + (n - offset) * beatsPerSample;
        return s;
    }

    const double target = tl.beatAt(blockTime);
    const double error = target - beat_;
    double b = beat_;
    if (!havePrediction_ || tl.epoch != epoch_ || std::abs(error) > kJumpSeconds * std::max(bps, 1e-6)) {
        b = target;
    } else {
        // Never backwards while playing.
        b = std::max(beat_ + kCorrection * error, lastReported_);
    }
    lastReported_ = b;
    epoch_ = tl.epoch;
    havePrediction_ = true;
    s.beat = b;
    s.playing = true;
    beat_ = b + n * beatsPerSample;
    return s;
}

}  // namespace spacer
