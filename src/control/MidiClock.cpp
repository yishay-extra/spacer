#include "control/MidiClock.h"

namespace spacer {

MidiClock::MidiClock(const ClockConfig& config, std::shared_ptr<ClockInputs::MidiQueue> input,
                     HostClock hostClock)
    : ExternalClock(config, std::move(hostClock)), input_(std::move(input)) {
    input_->attach();
}

MidiClock::~MidiClock() {
    input_->detach();
}

const ClockTimeline& MidiClock::poll(double now) noexcept {
    ClockMidiEvent e;
    while (input_->pop(e)) tracker_.handle(e.data, e.size, e.hostTime);
    tracker_.update(now);
    return tracker_.timeline();
}

}  // namespace spacer
