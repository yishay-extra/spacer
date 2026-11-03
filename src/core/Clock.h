#pragma once

namespace spacer {

/// Musical/transport time for one audio block. Produced by a ClockSource on
/// the audio thread; consumed by motions and the cue engine. See SPEC §9.
struct ClockState {
    double bpm{120.0};
    double beat{0.0};  // beats since transport start (quarter notes), fractional
    bool playing{false};
    double sampleRate{48000.0};
    double timeSeconds{0.0};  // wall-ish engine time since engine start, monotonic
    int beatsPerBar{4};
    /// Sample index within this block at which the transport starts (a
    /// quantised start, or play / continue), or -1 if it does not start in
    /// this block. SPEC §9.
    int startOffset{-1};
    /// Beat position at that start sample. Valid only when startOffset >= 0.
    /// A quantised start jumps from the frozen `beat` to the next bar
    /// boundary, so `beat` alone cannot tell a sample-accurate trigger where
    /// the transport begins. (Addition to SPEC §4 / §9, see the WP9 PR.)
    double startBeat{0.0};

    double bar() const { return beat / beatsPerBar; }
    double secondsPerBeat() const { return 60.0 / bpm; }
};

/// Abstract clock. Implementations: InternalClock (WP6, core), MidiClock,
/// MtcClock, OscClock and LinkClock (WP12, src/control). All methods are
/// called on the audio thread and must be real-time safe. Only one is active;
/// the engine switches clocks through SwapGraph (SPEC §9, Graph::setClock).
class ClockSource {
public:
    virtual ~ClockSource() = default;

    /// Called once per audio block before anything else. Advances the clock by
    /// numSamples and returns the state at the START of the block.
    virtual ClockState advance(int numSamples) = 0;

    /// Last state returned by advance().
    virtual ClockState current() const = 0;

    /// Called by the engine (audio thread, or while stopped) when the device
    /// rate changes and when the clock becomes the active one. Addition to
    /// SPEC §4 (WP12): external clocks need the rate to place `startOffset`.
    virtual void setSampleRate(double sampleRate) = 0;
};

}  // namespace spacer
