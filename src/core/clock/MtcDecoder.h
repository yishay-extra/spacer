#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include "core/clock/Timecode.h"

namespace spacer {

/// MIDI Time Code message decoder: assembles quarter-frame messages (F1 nn)
/// into full timecodes and parses full-frame SysEx (F0 7F dd 01 01 hh mm ss
/// ff F7). No allocation, no timing: MtcTracker adds that.
///
/// Quarter frames: piece 0 starts a set; a set is complete when pieces 0..7
/// arrived in order. The assembled time is the time of the frame at which
/// piece 0 was sent (MTC spec), so at the moment piece 7 arrives the sender
/// is 7 quarter frames (1.75 frames) further. Out-of-order pieces (reverse
/// play, lost bytes) discard the set.
class MtcDecoder {
public:
    struct Result {
        enum class Kind { None, QuarterFrame, FullSet, FullFrame } kind{Kind::None};
        int piece{-1};      // QuarterFrame / FullSet: the piece just received
        Timecode timecode;  // FullSet / FullFrame
    };

    /// One MIDI message (status byte first).
    Result handle(const std::uint8_t* data, int size);

    Result quarterFrame(std::uint8_t dataByte);
    static std::optional<Timecode> parseFullFrame(const std::uint8_t* data, int size);

    void reset();

private:
    std::array<std::uint8_t, 8> nibbles_{};
    int expected_{0};  // next piece expected in the current set
    bool inSet_{false};
};

}  // namespace spacer
