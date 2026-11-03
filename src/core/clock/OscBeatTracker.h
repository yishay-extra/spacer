#pragma once

#include "core/clock/ClockTimeline.h"

namespace spacer {

/// Follows a transport sent as beat positions (`/spacer/clock/beat f`, SPEC
/// §12.1) at an arbitrary, possibly irregular rate. An alpha-beta filter on
/// (host time, beat) pairs estimates position and tempo.
///
///  - The transport plays while successive messages advance the beat, and
///    stops when a message repeats the beat, when stop() is called or when no
///    message arrives for kTimeoutSeconds (beat frozen).
///  - A beat that disagrees with the prediction by more than kJumpBeats (or
///    goes backwards) is a locate: the estimate jumps (epoch++).
///  - Until two advancing messages arrived, the tempo is the configured one.
/// Not thread-safe; no allocation.
class OscBeatTracker {
public:
    static constexpr double kTimeoutSeconds = 1.0;
    static constexpr double kJumpBeats = 0.5;
    /// Position gain while acquiring (first 16 advancing messages) / locked.
    static constexpr double kAcquireAlpha = 0.2;
    static constexpr double kAlpha = 0.05;
    static constexpr double kBeta = 0.005;
    static constexpr double kMinBpm = 20.0;
    static constexpr double kMaxBpm = 300.0;

    explicit OscBeatTracker(double bpm = 120.0);

    void setBpm(double bpm);
    void beat(double beat, double hostTime);
    void stop(double hostTime);
    /// Call once per block with the current host time: handles the timeout.
    void update(double hostTime);

    const ClockTimeline& timeline() const { return timeline_; }

private:
    void locate(double beat, double hostTime);

    ClockTimeline timeline_;
    bool haveMessage_{false};
    double lastTime_{0.0};
    double lastBeat_{0.0};
    int advancing_{0};  // consecutive advancing messages
};

}  // namespace spacer
