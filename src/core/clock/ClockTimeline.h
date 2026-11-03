#pragma once

#include <algorithm>
#include <cstdint>

namespace spacer {

/// The transport of an external clock as a straight line in host time
/// (seconds on the host's monotonic clock): the beat at `anchorTime` and the
/// rate from there. Produced by the trackers (MidiClockTracker, MtcTracker,
/// OscBeatTracker, Link) and turned into per-block ClockState by
/// ClockFollower. Plain value type.
struct ClockTimeline {
    double anchorTime{0.0};      // host seconds
    double anchorBeat{0.0};      // beat at anchorTime
    double beatsPerSecond{2.0};  // rate while playing (120 bpm)
    /// Extrapolation past anchorTime is capped at this many seconds (the
    /// beat stalls when the reference stops arriving instead of running on).
    double maxAheadSeconds{1.0};
    bool playing{false};
    /// True once the tracker has a position and a tempo estimate.
    bool valid{false};
    /// True when the tracker considers itself locked (tempo settled).
    bool locked{false};
    /// Incremented on every discontinuity (start, locate, relock).
    std::uint32_t epoch{0};
    /// Host time and beat of the latest transport start (valid after one).
    double startTime{0.0};
    double startBeat{0.0};

    double beatAt(double hostTime) const {
        if (!playing) return anchorBeat;
        const double dt = std::min(hostTime - anchorTime, maxAheadSeconds);
        return anchorBeat + dt * beatsPerSecond;
    }
    double bpm() const { return beatsPerSecond * 60.0; }
};

}  // namespace spacer
