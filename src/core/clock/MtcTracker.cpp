#include "core/clock/MtcTracker.h"

#include <algorithm>
#include <cmath>

namespace spacer {

MtcTracker::MtcTracker(double bpm) {
    setBpm(bpm);
}

void MtcTracker::setBpm(double bpm) {
    if (!std::isfinite(bpm) || bpm <= 0.0) return;
    bpm_ = bpm;
    timeline_.anchorBeat = seconds_ * bpm_ / 60.0;
}

void MtcTracker::locate(double seconds, double hostTime, bool run) {
    seconds_ = seconds;
    running_ = run;
    setsSinceLocate_ = 0;
    if (run) {
        dll_.reset(hostTime, quarterFrameSeconds());
        timeline_.startTime = hostTime;
        timeline_.startBeat = seconds * bpm_ / 60.0;
    } else {
        dll_.clear();
    }
    timeline_.playing = run;
    timeline_.valid = true;
    timeline_.locked = false;
    timeline_.anchorTime = hostTime;
    timeline_.anchorBeat = seconds * bpm_ / 60.0;
    timeline_.beatsPerSecond = bpm_ / 60.0;
    timeline_.maxAheadSeconds = kTimeoutFrames / Timecode::framesPerSecond(rate_);
    ++timeline_.epoch;
}

void MtcTracker::handle(const std::uint8_t* data, int size, double hostTime) {
    const auto r = decoder_.handle(data, size);
    using Kind = MtcDecoder::Result::Kind;
    if (r.kind == Kind::None) return;
    if (r.kind == Kind::FullFrame) {
        rate_ = r.timecode.rate;
        locate(r.timecode.toSeconds(), hostTime, false);
        return;
    }

    if (r.kind == Kind::FullSet) {
        rate_ = r.timecode.rate;
        // Piece 0 was sent at the assembled frame; piece 7 is 7 quarter frames later.
        const double setSeconds = r.timecode.toSeconds() + 7.0 * quarterFrameSeconds();
        if (!running_) {
            lastQf_ = hostTime;
            locate(setSeconds, hostTime, true);
            return;
        }
        // The running position advances below; compare against where it will be.
        const double expected = seconds_ + quarterFrameSeconds();
        if (std::abs(expected - setSeconds) > 1.0 / Timecode::framesPerSecond(rate_)) {
            lastQf_ = hostTime;
            locate(setSeconds, hostTime, true);
            return;
        }
        ++setsSinceLocate_;
    }

    if (!running_) return;
    const double gap = hostTime - lastQf_;
    lastQf_ = hostTime;
    seconds_ += quarterFrameSeconds();
    // A gap of several quarter frames (lost bytes) counts as several units.
    const double units = std::max(1.0, std::round(gap / quarterFrameSeconds()));
    if (units > 1.0) seconds_ += (units - 1.0) * quarterFrameSeconds();
    const double filtered = dll_.update(hostTime, units, 0.5 * quarterFrameSeconds() * units);
    publish(filtered);
}

void MtcTracker::publish(double filteredTime) {
    const double speed = std::clamp(quarterFrameSeconds() / dll_.period(), 0.25, 4.0);
    timeline_.anchorTime = filteredTime;
    timeline_.anchorBeat = seconds_ * bpm_ / 60.0;
    timeline_.beatsPerSecond = speed * bpm_ / 60.0;
    timeline_.locked = setsSinceLocate_ >= 2;
}

void MtcTracker::update(double hostTime) {
    if (!running_) return;
    if (hostTime - lastQf_ > kTimeoutFrames / Timecode::framesPerSecond(rate_)) {
        running_ = false;
        timeline_.anchorBeat = timeline_.beatAt(hostTime);
        timeline_.anchorTime = hostTime;
        timeline_.playing = false;
        timeline_.locked = false;
        seconds_ = timeline_.anchorBeat * 60.0 / bpm_;
        decoder_.reset();
    }
}

}  // namespace spacer
