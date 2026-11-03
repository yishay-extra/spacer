#pragma once

#include <memory>

#include "control/ClockInputs.h"
#include "control/ExternalClock.h"
#include "core/clock/MidiClockTracker.h"

namespace spacer {

/// Follows MIDI beat clock (24 ppqn, Start / Stop / Continue / SPP; SPEC §9).
/// Input arrives timestamped through a ClockInputs queue; the PLL
/// (MidiClockTracker) runs on the audio thread.
class MidiClock final : public ExternalClock {
public:
    MidiClock(const ClockConfig& config, std::shared_ptr<ClockInputs::MidiQueue> input,
              HostClock hostClock = {});
    ~MidiClock() override;

    /// Tracker state (audio thread, or tests).
    const MidiClockTracker& tracker() const noexcept { return tracker_; }

private:
    const ClockTimeline& poll(double now) noexcept override;

    std::shared_ptr<ClockInputs::MidiQueue> input_;
    MidiClockTracker tracker_;
};

}  // namespace spacer
