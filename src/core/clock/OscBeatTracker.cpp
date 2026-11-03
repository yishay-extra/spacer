#include "core/clock/OscBeatTracker.h"

#include <algorithm>
#include <cmath>

namespace spacer {

OscBeatTracker::OscBeatTracker(double bpm) {
    timeline_.maxAheadSeconds = kTimeoutSeconds;
    setBpm(bpm);
}

void OscBeatTracker::setBpm(double bpm) {
    if (!std::isfinite(bpm)) return;
    timeline_.beatsPerSecond = std::clamp(bpm, kMinBpm, kMaxBpm) / 60.0;
}

void OscBeatTracker::locate(double b, double hostTime) {
    timeline_.anchorBeat = b;
    timeline_.anchorTime = hostTime;
    timeline_.valid = true;
    timeline_.locked = false;
    advancing_ = 0;
    ++timeline_.epoch;
}

void OscBeatTracker::beat(double b, double hostTime) {
    if (!std::isfinite(b) || !std::isfinite(hostTime)) return;
    const double minRate = kMinBpm / 60.0, maxRate = kMaxBpm / 60.0;

    if (!haveMessage_ || hostTime - lastTime_ > kTimeoutSeconds) {
        haveMessage_ = true;
        timeline_.playing = false;
        locate(b, hostTime);
    } else {
        const double dt = hostTime - lastTime_;
        const bool advanced = b > lastBeat_ + 1e-9;
        if (!advanced) {
            // Repeated beat: the sender's transport is stopped (or moved back).
            timeline_.playing = false;
            if (std::abs(b - lastBeat_) > 1e-9) locate(b, hostTime);
            timeline_.anchorBeat = b;
            timeline_.anchorTime = hostTime;
            advancing_ = 0;
        } else if (!timeline_.playing) {
            // Start: the transport runs from the previous message's beat on.
            if (dt > 0.0) timeline_.beatsPerSecond = std::clamp((b - lastBeat_) / dt, minRate, maxRate);
            timeline_.playing = true;
            timeline_.startTime = hostTime;
            timeline_.startBeat = b;
            timeline_.anchorBeat = b;
            timeline_.anchorTime = hostTime;
            timeline_.valid = true;
            advancing_ = 1;
            ++timeline_.epoch;
        } else {
            const double predicted =
                timeline_.anchorBeat + (hostTime - timeline_.anchorTime) * timeline_.beatsPerSecond;
            const double e = b - predicted;
            if (std::abs(e) > kJumpBeats) {
                locate(b, hostTime);
            } else {
                // Alpha-beta update. The first advancing intervals get a
                // larger beta so the tempo settles quickly.
                const double beta = advancing_ < 8 ? 0.3 : kBeta;
                const double alpha = advancing_ < 16 ? kAcquireAlpha : kAlpha;
                timeline_.anchorBeat = predicted + alpha * e;
                timeline_.anchorTime = hostTime;
                if (dt > 1e-4)
                    timeline_.beatsPerSecond =
                        std::clamp(timeline_.beatsPerSecond + beta * e / dt, minRate, maxRate);
                ++advancing_;
                timeline_.locked = advancing_ >= 16;
            }
        }
    }
    lastTime_ = hostTime;
    lastBeat_ = b;
}

void OscBeatTracker::stop(double hostTime) {
    if (!timeline_.playing) return;
    timeline_.anchorBeat = timeline_.beatAt(hostTime);
    timeline_.anchorTime = hostTime;
    timeline_.playing = false;
    advancing_ = 0;
}

void OscBeatTracker::update(double hostTime) {
    if (timeline_.playing && hostTime - lastTime_ > kTimeoutSeconds) {
        stop(hostTime);
        timeline_.locked = false;
    }
}

}  // namespace spacer
