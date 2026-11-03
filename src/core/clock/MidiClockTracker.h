#pragma once

#include <cstdint>

#include "core/clock/ClockTimeline.h"
#include "core/clock/TimeDll.h"

namespace spacer {

/// MIDI beat clock receiver (24 ppqn). Feeds Timing Clock (F8), Start (FA),
/// Continue (FB), Stop (FC) and Song Position Pointer (F2) messages with
/// host timestamps through a jitter-filtering DLL and publishes the result
/// as a ClockTimeline. See SPEC §9; decisions in the WP12 PR.
///
/// Transport semantics (MIDI 1.0 spec):
///  - Start sets the position to 0; Continue keeps it. Either way the
///    transport begins at the NEXT Timing Clock, which marks the position.
///  - Every Timing Clock while playing advances the position by 1/24 beat.
///  - Stop freezes the beat; SPP (only honoured while stopped) moves it, in
///    sixteenth notes (6 clocks).
///  - Tempo is tracked from Timing Clock whether or not the transport runs
///    (senders keep sending clock while stopped).
///
/// Lock: the loop starts wide (fast acquisition) and narrows over the first
/// two bars; a gap in the clock or a run of large errors (tempo jump)
/// restarts acquisition. Not thread-safe; the owner calls it from the audio
/// thread with events handed over lock-free. No allocation.
class MidiClockTracker {
public:
    static constexpr int kPpqn = 24;
    /// Clock silent this long: restart acquisition, beat stalls.
    static constexpr double kDropoutSeconds = 0.5;
    static constexpr double kWideBandwidthHz = 4.0;
    static constexpr double kNarrowBandwidthHz = 0.25;
    /// Ticks after (re)acquisition over which the bandwidth narrows.
    static constexpr int kAcquireTicks = 96;
    /// Ticks before `locked` can become true.
    static constexpr int kLockTicks = 48;

    MidiClockTracker();

    /// One MIDI message (status byte first). Non-clock messages are ignored.
    void handle(const std::uint8_t* data, int size, double hostTime);

    void tick(double hostTime);
    void start();
    void resume();
    void stop(double hostTime);
    /// Song position in MIDI beats (sixteenth notes).
    void songPosition(int sixteenths);

    /// Call once per block with the current host time: handles dropouts.
    void update(double hostTime);

    const ClockTimeline& timeline() const { return timeline_; }
    bool locked() const { return timeline_.locked; }
    double bpm() const { return timeline_.bpm(); }
    /// Position in clocks of the next Timing Clock.
    std::int64_t position() const { return position_; }

private:
    void acquire(double hostTime);
    void publish(double filteredTime);

    TimeDll dll_;
    ClockTimeline timeline_;
    std::int64_t position_{0};  // clocks; the next tick marks this position
    bool startPending_{false};
    bool haveTick_{false};
    double lastRawTick_{0.0};
    int ticksSinceAcquire_{0};
    int outliers_{0};
    double errorAverage_{0.0};  // running mean |phase error| / period
};

}  // namespace spacer
