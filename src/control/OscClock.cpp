#include "control/OscClock.h"

namespace spacer {

OscClock::OscClock(const ClockConfig& config, std::shared_ptr<ClockInputs::OscQueue> input,
                   HostClock hostClock)
    : ExternalClock(config, std::move(hostClock)), input_(std::move(input)), tracker_(config.bpm) {
    input_->attach();
}

OscClock::~OscClock() {
    input_->detach();
}

const ClockTimeline& OscClock::poll(double now) noexcept {
    ClockOscEvent e;
    while (input_->pop(e)) {
        if (e.kind == ClockOscEvent::Kind::Beat)
            tracker_.beat(e.beat, e.hostTime);
        else
            tracker_.stop(e.hostTime);
    }
    tracker_.update(now);
    return tracker_.timeline();
}

}  // namespace spacer
