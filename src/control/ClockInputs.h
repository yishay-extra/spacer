#pragma once

#include <cstdint>
#include <memory>

#include "control/ClockEventQueue.h"
#include "control/ExternalClock.h"
#include "core/Project.h"

namespace spacer {

/// Feeder side of the external clocks (WP12) and their factory. The app owns
/// one; MIDI and OSC receive threads hand clock data to it, and the engine's
/// clock factory (AudioEngine::setClockFactory) creates the ClockSource for
/// the project's ClockConfig from it.
///
/// The queues are shared with the clocks (shared_ptr), so a clock deleted
/// with a retired graph never leaves a producer writing into freed memory.
/// Every handle* method is safe from any non-audio thread and never blocks
/// on the audio thread.
class ClockInputs {
public:
    using MidiQueue = ClockEventQueue<ClockMidiEvent, 2048>;
    using OscQueue = ClockEventQueue<ClockOscEvent, 1024>;

    ClockInputs();

    /// A MIDI message with its host timestamp in seconds (JUCE: the
    /// MidiMessage timestamp from a MidiInput callback). Returns true if it
    /// is clock data (beat clock, MTC, SPP) — such messages need not go to
    /// the MIDI mapper.
    bool handleMidi(const std::uint8_t* data, int size, double hostTime) noexcept;
    /// `/spacer/clock/beat` at host time. True if an OscClock consumed it
    /// (then it must not also be applied to the internal clock).
    bool handleOscBeat(double beat, double hostTime) noexcept;
    /// `/spacer/clock/stop`. True if an OscClock is active.
    bool handleOscStop(double hostTime) noexcept;
    bool oscClockActive() const noexcept { return osc_->hasConsumer(); }

    /// Message thread. Null for Internal, and for Link when it is not compiled in.
    std::unique_ptr<ClockSource> makeClock(const ClockConfig& config,
                                           ExternalClock::HostClock hostClock = {}) const;

    /// Lock status of the external clock that ran last (any thread).
    const ExternalClockStatus& status() const noexcept { return *status_; }

    static bool isBeatClockMessage(const std::uint8_t* data, int size) noexcept;
    static bool isTimecodeMessage(const std::uint8_t* data, int size) noexcept;
    /// True when built with SPACER_WITH_LINK.
    static bool linkAvailable() noexcept;

private:
    std::shared_ptr<MidiQueue> beatClock_;
    std::shared_ptr<MidiQueue> timecode_;
    std::shared_ptr<OscQueue> osc_;
    std::shared_ptr<ExternalClockStatus> status_;
};

}  // namespace spacer
