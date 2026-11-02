#pragma once

namespace spacer {

/// Musical/transport time for one audio block. Produced by a ClockSource on
/// the audio thread; consumed by motions and the cue engine. See SPEC §9.
struct ClockState {
    double bpm{120.0};
    double beat{0.0};          // beats since transport start (quarter notes), fractional
    bool playing{false};
    double sampleRate{48000.0};
    double timeSeconds{0.0};   // wall-ish engine time since engine start, monotonic
    int beatsPerBar{4};

    double bar() const { return beat / beatsPerBar; }
    double secondsPerBeat() const { return 60.0 / bpm; }
};

/// Abstract clock. Implementations: InternalClock (WP6), LinkClock, MidiClock,
/// MtcClock, OscClock (WP12). All methods are called on the audio thread and
/// must be real-time safe.
class ClockSource {
public:
    virtual ~ClockSource() = default;

    /// Called once per audio block before anything else. Advances the clock by
    /// numSamples and returns the state at the START of the block.
    virtual ClockState advance(int numSamples) = 0;

    /// Last state returned by advance().
    virtual ClockState current() const = 0;
};

}  // namespace spacer
