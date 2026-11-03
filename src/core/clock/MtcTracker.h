#pragma once

#include <cstdint>

#include "core/clock/ClockTimeline.h"
#include "core/clock/MtcDecoder.h"
#include "core/clock/TimeDll.h"

namespace spacer {

/// MIDI Time Code follower: decodes MTC (MtcDecoder), filters the quarter
/// frame arrival times with a DLL and publishes a ClockTimeline in beats.
///
/// MTC carries absolute time, not tempo, so beats are derived from the
/// configured tempo: beat = timecodeSeconds * bpm / 60 (timecode 00:00:00:00
/// = beat 0). The rate is scaled by the measured play speed (normally 1).
///
///  - Running: from the first complete quarter-frame set (2 frames) on;
///    every later quarter frame advances the position by 1/4 frame.
///  - Each complete set is checked against the running position; a mismatch
///    over one frame is a locate (jump, epoch++).
///  - No quarter frame for kTimeoutFrames frames: stopped, beat frozen.
///  - Full-frame SysEx: locate while stopped.
/// Not thread-safe; no allocation.
class MtcTracker {
public:
    static constexpr double kTimeoutFrames = 4.0;
    static constexpr double kBandwidthHz = 0.5;

    explicit MtcTracker(double bpm = 120.0);

    void setBpm(double bpm);
    void handle(const std::uint8_t* data, int size, double hostTime);
    /// Call once per block with the current host time: handles the timeout.
    void update(double hostTime);

    const ClockTimeline& timeline() const { return timeline_; }
    /// Timecode seconds at the last quarter frame.
    double seconds() const { return seconds_; }
    TimecodeRate rate() const { return rate_; }
    bool running() const { return running_; }

private:
    void locate(double seconds, double hostTime, bool run);
    void publish(double filteredTime);
    double quarterFrameSeconds() const { return 0.25 / Timecode::framesPerSecond(rate_); }

    MtcDecoder decoder_;
    TimeDll dll_{kBandwidthHz};
    ClockTimeline timeline_;
    double bpm_{120.0};
    TimecodeRate rate_{TimecodeRate::Fps25};
    double seconds_{0.0};  // timecode seconds at the last quarter frame
    double lastQf_{0.0};   // raw host time of the last quarter frame
    bool running_{false};
    int setsSinceLocate_{0};
};

}  // namespace spacer
