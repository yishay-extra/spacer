#include "control/link/LinkClock.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace spacer {

namespace {
std::chrono::microseconds toMicros(double seconds) {
    return std::chrono::microseconds(static_cast<long long>(std::llround(seconds * 1e6)));
}
}  // namespace

LinkClock::LinkClock(const ClockConfig& config)
    : ExternalClock(config, [this] { return static_cast<double>(link_.clock().micros().count()) * 1e-6; }),
      link_(config.bpm),
      quantum_(static_cast<double>(std::max(1, config.beatsPerBar))) {
    link_.enableStartStopSync(true);
    link_.enable(true);
    timeline_.maxAheadSeconds = 1.0;
}

LinkClock::~LinkClock() {
    link_.enable(false);
}

const ClockTimeline& LinkClock::poll(double now) noexcept {
    const auto state = link_.captureAudioSessionState();
    const auto t = toMicros(now);
    const bool playing = state.isPlaying();
    timeline_.valid = true;
    timeline_.locked = link_.numPeers() > 0;
    timeline_.beatsPerSecond = state.tempo() / 60.0;
    if (playing && !wasPlaying_) {
        const auto start = state.timeForIsPlaying();
        timeline_.startTime = static_cast<double>(start.count()) * 1e-6;
        timeline_.startBeat = state.beatAtTime(start, quantum_);
        ++timeline_.epoch;
    }
    if (playing) {
        timeline_.anchorTime = now;
        timeline_.anchorBeat = state.beatAtTime(t, quantum_);
    } else if (wasPlaying_) {
        // Stopped: the reported beat stays where the session stopped.
        const auto stop = state.timeForIsPlaying();
        timeline_.anchorTime = static_cast<double>(stop.count()) * 1e-6;
        timeline_.anchorBeat = state.beatAtTime(stop, quantum_);
    }
    timeline_.playing = playing;
    wasPlaying_ = playing;
    return timeline_;
}

}  // namespace spacer
