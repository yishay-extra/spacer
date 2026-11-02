#include "core/InternalClock.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>

namespace spacer {

InternalClock::InternalClock(double sampleRate, int beatsPerBar) {
    assert(sampleRate > 0.0);
    state_.sampleRate = sampleRate;
    state_.beatsPerBar = std::max(1, beatsPerBar);
    last_ = state_;
}

void InternalClock::configure(const ClockConfig& config) {
    setBpm(config.bpm);
    setBeatsPerBar(config.beatsPerBar);
    setQuantisedStart(config.quantisedStart);
}

void InternalClock::setSampleRate(double sampleRate) {
    assert(sampleRate > 0.0);
    state_.sampleRate = sampleRate;
}

void InternalClock::setBpm(double bpm) {
    if (!std::isfinite(bpm)) return;
    state_.bpm = std::clamp(bpm, kMinBpm, kMaxBpm);
}

void InternalClock::setBeatsPerBar(int beatsPerBar) {
    state_.beatsPerBar = std::max(1, beatsPerBar);
}

void InternalClock::setBeat(double beat) {
    if (!std::isfinite(beat)) return;
    state_.beat = beat;
    if (armed_) arm(beat);
}

void InternalClock::arm(double fromBeat) {
    pendingBeat_ = nextBarBoundary(fromBeat);
    armedBeatsToGo_ = pendingBeat_ - fromBeat;
    armed_ = true;
}

void InternalClock::play() {
    if (state_.playing || armed_) return;
    if (quantisedStart_) {
        arm(state_.beat);
    } else {
        state_.playing = true;
        startNow_ = true;
    }
}

void InternalClock::resume() {
    armed_ = false;
    if (!state_.playing) startNow_ = true;
    state_.playing = true;
}

void InternalClock::stop() {
    armed_ = false;
    startNow_ = false;
    state_.playing = false;
}

void InternalClock::tap(double timeSeconds) {
    if (!std::isfinite(timeSeconds)) return;

    const bool continuesSeries =
        haveTap_ && timeSeconds > lastTap_ && timeSeconds - lastTap_ <= kTapTimeoutSeconds;
    if (continuesSeries) {
        tapIntervals_[static_cast<std::size_t>(nextTapInterval_)] = timeSeconds - lastTap_;
        nextTapInterval_ = (nextTapInterval_ + 1) % kTapHistory;
        tapIntervalCount_ = std::min(tapIntervalCount_ + 1, kTapHistory);

        double sum = 0.0;
        for (int i = 0; i < tapIntervalCount_; ++i) {
            sum += tapIntervals_[static_cast<std::size_t>(i)];
        }
        const double meanInterval = sum / tapIntervalCount_;
        setBpm(60.0 / meanInterval);
    } else {
        tapIntervalCount_ = 0;
        nextTapInterval_ = 0;
    }
    lastTap_ = timeSeconds;
    haveTap_ = true;
}

ClockState InternalClock::advance(int numSamples) {
    last_ = state_;
    last_.startOffset = -1;
    if (numSamples <= 0) return last_;

    if (startNow_) {
        startNow_ = false;
        last_.startOffset = 0;
        last_.startBeat = last_.beat;
    }

    const double sr = state_.sampleRate;
    state_.timeSeconds += static_cast<double>(numSamples) / sr;

    if (armed_) {
        // The reported beat stays frozen while waiting. Count down to the
        // boundary; if it falls inside this block, start there and carry the
        // rest of the block into the beat.
        const double blockBeats = beatsForSamples(numSamples);
        if (armedBeatsToGo_ <= blockBeats) {
            const double offset = std::round(armedBeatsToGo_ / blockBeats * numSamples);
            last_.startOffset = std::clamp(static_cast<int>(offset), 0, numSamples - 1);
            last_.startBeat = pendingBeat_;
            armed_ = false;
            state_.playing = true;
            state_.beat = pendingBeat_ + (blockBeats - armedBeatsToGo_);
        } else {
            armedBeatsToGo_ -= blockBeats;
        }
    } else if (state_.playing) {
        state_.beat += beatsForSamples(numSamples);
    }
    return last_;
}

double InternalClock::nextBarBoundary(double beat) const {
    const double bar = static_cast<double>(state_.beatsPerBar);
    // The tolerance keeps a beat that is on a boundary (up to float error) on
    // that boundary instead of skipping to the next one.
    const double bars = std::ceil(beat / bar - 1e-9);
    return std::max(bars * bar, beat);
}

double InternalClock::beatsForSamples(int numSamples) const {
    return static_cast<double>(numSamples) / state_.sampleRate / state_.secondsPerBeat();
}

}  // namespace spacer
