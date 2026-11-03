#include "control/MtcClock.h"

namespace spacer {

MtcClock::MtcClock(const ClockConfig& config, std::shared_ptr<ClockInputs::MidiQueue> input,
                   HostClock hostClock)
    : ExternalClock(config, std::move(hostClock)), input_(std::move(input)), tracker_(config.bpm) {
    input_->attach();
}

MtcClock::~MtcClock() {
    input_->detach();
}

const ClockTimeline& MtcClock::poll(double now) noexcept {
    ClockMidiEvent e;
    while (input_->pop(e)) tracker_.handle(e.data, e.size, e.hostTime);
    tracker_.update(now);
    return tracker_.timeline();
}

}  // namespace spacer
