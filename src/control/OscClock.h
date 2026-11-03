#pragma once

#include <memory>

#include "control/ClockInputs.h"
#include "control/ExternalClock.h"
#include "core/clock/OscBeatTracker.h"

namespace spacer {

/// Follows beat positions sent as `/spacer/clock/beat f` (SPEC §9, §12.1).
/// The OSC receive thread timestamps each message on arrival (or from its
/// bundle timetag) and hands it over through a ClockInputs queue.
class OscClock final : public ExternalClock {
public:
    OscClock(const ClockConfig& config, std::shared_ptr<ClockInputs::OscQueue> input,
             HostClock hostClock = {});
    ~OscClock() override;

    const OscBeatTracker& tracker() const noexcept { return tracker_; }

private:
    const ClockTimeline& poll(double now) noexcept override;

    std::shared_ptr<ClockInputs::OscQueue> input_;
    OscBeatTracker tracker_;
};

}  // namespace spacer
