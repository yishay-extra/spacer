#pragma once

#include <string_view>

#include "core/Clock.h"
#include "core/Position.h"

namespace spacer {

/// Everything a motion may read when evaluating. Built by the engine per block.
struct MotionContext {
    ClockState clock;        // state at the START of the block
    int numSamples{0};
    double sampleRate{48000.0};

    double blockSeconds() const { return numSamples / sampleRate; }
    double blockBeats() const { return clock.playing ? blockSeconds() / clock.secondsPerBeat() : 0.0; }
};

/// Drives one source's position over time. See SPEC §10. One instance per
/// source; evaluate() runs on the audio thread and must be real-time safe.
class Motion {
public:
    virtual ~Motion() = default;

    virtual std::string_view name() const = 0;

    /// Non-RT. Reset internal phase (e.g. on cue recall with "restart phase").
    virtual void reset() = 0;

    /// RT. Return the position at the END of this block. The engine
    /// interpolates from the previous block's end position to this one.
    virtual Position evaluate(const MotionContext& ctx) = 0;

    /// Non-RT. Position the motion currently holds, for UI/OSC readback.
    virtual Position current() const = 0;
};

}  // namespace spacer
