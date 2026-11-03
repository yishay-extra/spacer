#include "control/ClockInputs.h"

#include <algorithm>
#include <cstring>

#include "control/MidiClock.h"
#include "control/MtcClock.h"
#include "control/OscClock.h"

#ifndef SPACER_WITH_LINK
#define SPACER_WITH_LINK 0
#endif
#if SPACER_WITH_LINK
#include "control/link/LinkClock.h"
#endif

namespace spacer {

ClockInputs::ClockInputs()
    : beatClock_(std::make_shared<MidiQueue>()),
      timecode_(std::make_shared<MidiQueue>()),
      osc_(std::make_shared<OscQueue>()),
      status_(std::make_shared<ExternalClockStatus>()) {}

bool ClockInputs::isBeatClockMessage(const std::uint8_t* d, int size) noexcept {
    if (d == nullptr || size < 1) return false;
    return d[0] == 0xF8 || d[0] == 0xFA || d[0] == 0xFB || d[0] == 0xFC || (d[0] == 0xF2 && size >= 3);
}

bool ClockInputs::isTimecodeMessage(const std::uint8_t* d, int size) noexcept {
    if (d == nullptr || size < 1) return false;
    if (d[0] == 0xF1) return size >= 2;
    // Full-frame SysEx: F0 7F <device> 01 01 hh mm ss ff F7
    return size == 10 && d[0] == 0xF0 && d[1] == 0x7F && d[3] == 0x01 && d[4] == 0x01;
}

bool ClockInputs::handleMidi(const std::uint8_t* data, int size, double hostTime) noexcept {
    const bool beat = isBeatClockMessage(data, size);
    const bool tc = !beat && isTimecodeMessage(data, size);
    if (!beat && !tc) return false;
    ClockMidiEvent e;
    e.hostTime = hostTime;
    e.size = static_cast<std::uint8_t>(std::min(size, static_cast<int>(sizeof(e.data))));
    std::memcpy(e.data, data, e.size);
    (beat ? beatClock_ : timecode_)->push(e);
    return true;
}

bool ClockInputs::handleOscBeat(double beat, double hostTime) noexcept {
    if (!osc_->hasConsumer()) return false;
    osc_->push(ClockOscEvent{hostTime, beat, ClockOscEvent::Kind::Beat});
    return true;
}

bool ClockInputs::handleOscStop(double hostTime) noexcept {
    if (!osc_->hasConsumer()) return false;
    osc_->push(ClockOscEvent{hostTime, 0.0, ClockOscEvent::Kind::Stop});
    return true;
}

std::unique_ptr<ClockSource> ClockInputs::makeClock(const ClockConfig& config,
                                                    ExternalClock::HostClock hostClock) const {
    std::unique_ptr<ExternalClock> clock;
    switch (config.kind) {
        case ClockKind::Internal:
            break;
        case ClockKind::Midi:
            clock = std::make_unique<MidiClock>(config, beatClock_, std::move(hostClock));
            break;
        case ClockKind::Mtc:
            clock = std::make_unique<MtcClock>(config, timecode_, std::move(hostClock));
            break;
        case ClockKind::Osc:
            clock = std::make_unique<OscClock>(config, osc_, std::move(hostClock));
            break;
        case ClockKind::Link:
#if SPACER_WITH_LINK
            clock = std::make_unique<LinkClock>(config);
#endif
            break;
    }
    if (clock) clock->setStatus(status_);
    return clock;
}

bool ClockInputs::linkAvailable() noexcept {
    return SPACER_WITH_LINK != 0;
}

}  // namespace spacer
