#include "core/clock/MidiClockTracker.h"

#include <algorithm>
#include <cmath>

namespace spacer {

namespace {
// Tick period limits: 20..300 bpm (SPEC §9 tempo range), with margin.
constexpr double kMinTickPeriod = 60.0 / (400.0 * MidiClockTracker::kPpqn);
constexpr double kMaxTickPeriod = 60.0 / (15.0 * MidiClockTracker::kPpqn);
}  // namespace

MidiClockTracker::MidiClockTracker() : dll_(kWideBandwidthHz) {
    timeline_.maxAheadSeconds = 0.1;
}

void MidiClockTracker::handle(const std::uint8_t* data, int size, double hostTime) {
    if (data == nullptr || size < 1) return;
    switch (data[0]) {
        case 0xF8:
            tick(hostTime);
            break;
        case 0xFA:
            start();
            break;
        case 0xFB:
            resume();
            break;
        case 0xFC:
            stop(hostTime);
            break;
        case 0xF2:
            if (size >= 3) songPosition((data[1] & 0x7F) | ((data[2] & 0x7F) << 7));
            break;
        default:
            break;
    }
}

void MidiClockTracker::start() {
    position_ = 0;
    startPending_ = true;
}

void MidiClockTracker::resume() {
    startPending_ = true;
}

void MidiClockTracker::stop(double hostTime) {
    startPending_ = false;
    if (!timeline_.playing) return;
    // Freeze where the transport is, but never past the next clock's position
    // (Continue resumes there) nor before the last one.
    const double next = static_cast<double>(position_) / kPpqn;
    const double beat = std::clamp(timeline_.beatAt(hostTime), next - 1.0 / kPpqn, next);
    timeline_.playing = false;
    timeline_.anchorBeat = beat;
    timeline_.anchorTime = hostTime;
}

void MidiClockTracker::songPosition(int sixteenths) {
    if (timeline_.playing) return;  // SPP is only meaningful while stopped
    position_ = static_cast<std::int64_t>(std::max(0, sixteenths)) * 6;
    timeline_.anchorBeat = static_cast<double>(position_) / kPpqn;
    ++timeline_.epoch;
}

void MidiClockTracker::acquire(double hostTime) {
    dll_.clear();
    dll_.setBandwidth(kWideBandwidthHz);
    ticksSinceAcquire_ = 0;
    outliers_ = 0;
    errorAverage_ = 1.0;
    timeline_.locked = false;
    lastRawTick_ = hostTime;
}

void MidiClockTracker::tick(double hostTime) {
    double filtered = hostTime;
    if (!haveTick_ || hostTime - lastRawTick_ > kDropoutSeconds || hostTime <= lastRawTick_ - 0.1) {
        acquire(hostTime);
        haveTick_ = true;
    } else if (!dll_.started()) {
        // Second tick: first period estimate.
        const double dt = hostTime - lastRawTick_;
        dll_.reset(hostTime, std::clamp(dt, kMinTickPeriod, kMaxTickPeriod));
        filtered = hostTime;
        ++ticksSinceAcquire_;
    } else {
        const double period = dll_.period();
        filtered = dll_.update(hostTime, 1.0, 0.5 * period);
        const double rel = std::abs(dll_.lastError()) / period;
        // A run of large errors means the tempo jumped: re-acquire.
        outliers_ = rel > 0.35 ? outliers_ + 1 : 0;
        if (outliers_ >= 3) {
            const double dt = hostTime - lastRawTick_;
            acquire(hostTime);
            dll_.reset(hostTime, std::clamp(dt, kMinTickPeriod, kMaxTickPeriod));
            filtered = hostTime;
        }
        errorAverage_ += (std::min(rel, 1.0) - errorAverage_) * 0.05;
        ++ticksSinceAcquire_;
        // Narrow the loop geometrically over the acquisition window.
        const double t = std::min(1.0, static_cast<double>(ticksSinceAcquire_) / kAcquireTicks);
        dll_.setBandwidth(kWideBandwidthHz * std::pow(kNarrowBandwidthHz / kWideBandwidthHz, t));
    }
    lastRawTick_ = hostTime;
    publish(filtered);
}

void MidiClockTracker::publish(double filteredTime) {
    if (dll_.started()) {
        timeline_.beatsPerSecond = 1.0 / (dll_.period() * kPpqn);
        timeline_.valid = true;
    }
    timeline_.locked = ticksSinceAcquire_ >= kLockTicks && errorAverage_ < 0.1;

    if (startPending_) {
        startPending_ = false;
        timeline_.playing = true;
        timeline_.startTime = filteredTime;
        timeline_.startBeat = static_cast<double>(position_) / kPpqn;
        ++timeline_.epoch;
    }
    if (timeline_.playing) {
        timeline_.anchorTime = filteredTime;
        timeline_.anchorBeat = static_cast<double>(position_) / kPpqn;
        ++position_;
    }
    // Extrapolate up to ~3 clocks: covers delivery latency, stalls on dropout.
    timeline_.maxAheadSeconds = 3.0 / (timeline_.beatsPerSecond * kPpqn);
}

void MidiClockTracker::update(double hostTime) {
    if (haveTick_ && hostTime - lastRawTick_ > kDropoutSeconds) {
        timeline_.locked = false;
        if (timeline_.playing) {
            timeline_.anchorBeat = timeline_.beatAt(hostTime);
            timeline_.anchorTime = hostTime;
            timeline_.maxAheadSeconds = 0.0;
        }
    }
}

}  // namespace spacer
