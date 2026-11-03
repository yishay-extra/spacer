#pragma once

#include <memory>

#include "control/ClockInputs.h"
#include "control/ExternalClock.h"
#include "core/clock/MtcTracker.h"

namespace spacer {

/// Follows MIDI Time Code (SPEC §9). MTC has no tempo: beats are timecode
/// seconds at the project tempo (ClockConfig::bpm), 00:00:00:00 = beat 0.
class MtcClock final : public ExternalClock {
public:
    MtcClock(const ClockConfig& config, std::shared_ptr<ClockInputs::MidiQueue> input,
             HostClock hostClock = {});
    ~MtcClock() override;

    const MtcTracker& tracker() const noexcept { return tracker_; }

private:
    const ClockTimeline& poll(double now) noexcept override;

    std::shared_ptr<ClockInputs::MidiQueue> input_;
    MtcTracker tracker_;
};

}  // namespace spacer
