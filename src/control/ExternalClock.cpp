#include "control/ExternalClock.h"

#include <juce_core/juce_core.h>

#include <algorithm>

namespace spacer {

ExternalClock::ExternalClock(const ClockConfig& config, HostClock hostClock)
    : kind_(config.kind), hostClock_(std::move(hostClock)), beatsPerBar_(std::max(1, config.beatsPerBar)) {
    last_.bpm = config.bpm;
    last_.beatsPerBar = beatsPerBar_;
    last_.sampleRate = sampleRate_;
    timer_.prepare(sampleRate_);
}

double ExternalClock::defaultHostTime() {
    return juce::Time::getMillisecondCounterHiRes() * 0.001;
}

void ExternalClock::setSampleRate(double sampleRate) {
    if (!(sampleRate > 0.0)) return;
    sampleRate_ = sampleRate;
    timer_.prepare(sampleRate);
    last_.sampleRate = sampleRate;
}

ClockState ExternalClock::advance(int numSamples) {
    if (numSamples <= 0) {
        last_.startOffset = -1;
        return last_;
    }
    const double t = now();
    const double blockTime = timer_.blockStart(t, numSamples);
    const ClockTimeline& tl = poll(t);
    locked_.store(tl.locked, std::memory_order_relaxed);
    if (status_) {
        status_->locked.store(tl.locked, std::memory_order_relaxed);
        status_->kind.store(static_cast<int>(kind_), std::memory_order_relaxed);
    }
    last_ = follower_.follow(tl, blockTime, numSamples, sampleRate_, timer_.secondsPerSample(), beatsPerBar_);
    // The engine overrides timeSeconds with its own timeline; keep a
    // monotonic one here too for direct users.
    last_.timeSeconds = timeSeconds_;
    timeSeconds_ += numSamples / sampleRate_;
    return last_;
}

}  // namespace spacer
