#pragma once

// Audio → control feedback (SPEC §5.2). Published by the audio thread at
// about 60 Hz of audio time and polled by the message thread. Small PODs only.

#include <cstdint>
#include <type_traits>
#include <variant>

#include "core/Clock.h"
#include "core/Position.h"

namespace spacer {

namespace fb {

/// Peak absolute sample per source channel since the last report, post gain
/// and distance (what the spatializer received).
struct SourceMeter {
    int sourceId{0};
    float peak[2]{0.0f, 0.0f};
};
/// Peak at the speaker output (post limiter, pre master) and the largest
/// limiter gain reduction since the last report.
struct SpeakerMeter {
    int speakerId{0};
    float peak{0.0f};
    float gainReductionDb{0.0f};
};
/// Rendered position of a source channel at the end of the last block.
struct SourcePosition {
    int sourceId{0};
    int channel{0};
    Position position;
};
struct Clock {
    ClockState state;
};
/// Device overload count, as reported by the device (monotonic).
struct Xrun {
    int total{0};
};
/// Commands dropped because the command queue was full (monotonic).
struct CommandDropped {
    std::uint64_t total{0};
};

/// A recall with a non-zero token has been applied (immediately, or at its
/// scheduled beat).
struct CueFired {
    int token{0};
};

}  // namespace fb

using Feedback = std::variant<fb::SourceMeter, fb::SpeakerMeter, fb::SourcePosition, fb::Clock, fb::Xrun,
                              fb::CommandDropped, fb::CueFired>;

static_assert(std::is_trivially_copyable_v<Feedback>);

}  // namespace spacer
