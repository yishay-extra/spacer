#pragma once

#include <atomic>
#include <functional>
#include <memory>

#include "core/Clock.h"
#include "core/Project.h"
#include "core/clock/BlockTimer.h"
#include "core/clock/ClockFollower.h"
#include "core/clock/ClockTimeline.h"

namespace spacer {

/// Base of the WP12 clocks (MidiClock, MtcClock, OscClock, LinkClock): a
/// ClockSource whose transport follows an external reference (SPEC §9).
///
/// Per block (audio thread): read the host clock, filter it into the block's
/// start time (BlockTimer), let the subclass drain its lock-free input and
/// update its tracker (poll), and turn the tracker's ClockTimeline into the
/// block's ClockState (ClockFollower: smooth, sample-accurate startOffset).
///
/// Host time is in seconds of the host's monotonic clock; by default
/// juce::Time::getMillisecondCounterHiRes() / 1000, the same timeline as
/// JUCE's MIDI input timestamps. Tests inject their own.
/// Status of the active external clock, readable from any thread (the UI
/// polls it). Written by the clock on the audio thread.
struct ExternalClockStatus {
    std::atomic<bool> locked{false};
    std::atomic<int> kind{static_cast<int>(ClockKind::Internal)};  // ClockKind of the clock that wrote last
};

class ExternalClock : public ClockSource {
public:
    using HostClock = std::function<double()>;

    explicit ExternalClock(const ClockConfig& config, HostClock hostClock = {});

    /// Message thread, before the clock is handed to the engine.
    void setStatus(std::shared_ptr<ExternalClockStatus> status) { status_ = std::move(status); }

    ClockState advance(int numSamples) final;
    ClockState current() const final { return last_; }
    void setSampleRate(double sampleRate) override;

    /// Any thread: the tracker reports a settled lock.
    bool locked() const noexcept { return locked_.load(std::memory_order_relaxed); }
    ClockKind kind() const noexcept { return kind_; }

    static double defaultHostTime();

protected:
    /// Audio thread. Drain queued input, update the tracker for host time
    /// `now`, return the reference timeline.
    virtual const ClockTimeline& poll(double now) noexcept = 0;
    double now() const { return hostClock_ ? hostClock_() : defaultHostTime(); }

private:
    ClockKind kind_;
    HostClock hostClock_;
    BlockTimer timer_;
    ClockFollower follower_;
    ClockState last_;
    double sampleRate_{48000.0};
    double timeSeconds_{0.0};
    int beatsPerBar_{4};
    std::atomic<bool> locked_{false};
    std::shared_ptr<ExternalClockStatus> status_;
};

}  // namespace spacer
