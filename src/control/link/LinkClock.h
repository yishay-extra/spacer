#pragma once

// Only built with -DSPACER_WITH_LINK=ON (Ableton Link is GPLv2+ or
// proprietary-by-agreement; see docs/mac-validation/WP12.md and the WP12 PR).

#include <ableton/Link.hpp>

#include "control/ExternalClock.h"

namespace spacer {

/// Follows an Ableton Link session (SPEC §9): tempo and beat phase from the
/// session, transport from Link start/stop sync. The quantum is the
/// project's beatsPerBar. Spacer joins as a follower: it never changes the
/// session tempo or transport.
///
/// Link's audio-thread API (captureAudioSessionState) is real-time safe; host
/// time is Link's own clock, filtered per block like the other clocks.
/// Construction / destruction (network threads) happen on the message
/// thread, with the graph that owns the clock.
class LinkClock final : public ExternalClock {
public:
    explicit LinkClock(const ClockConfig& config);
    ~LinkClock() override;

    std::size_t numPeers() const { return link_.numPeers(); }

private:
    const ClockTimeline& poll(double now) noexcept override;

    ableton::Link link_;
    double quantum_;
    ClockTimeline timeline_;
    bool wasPlaying_{false};
};

}  // namespace spacer
