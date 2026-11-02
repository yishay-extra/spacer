#pragma once

#include <juce_dsp/juce_dsp.h>

#include <array>

#include "core/SpeakerChain.h"
#include "engine/GainRamp.h"
#include "engine/LookaheadLimiter.h"

namespace spacer {

/// Everything one speaker chain needs from the model (built by GraphBuilder).
struct SpeakerChainSettings {
    int speakerId{0};
    float compensationDelayMs{0.0f};  // computeCompensation (SPEC §7)
    float compensationGainDb{0.0f};
    float userDelayMs{0.0f};
    float trimDb{0.0f};
    bool polarityInverted{false};
    bool mute{false};
    Eq3 eq;
};

/// Per-speaker output processing (SPEC §5.3):
///   delay (compensation + user, fractional, max 100 ms, 50 ms crossfade on change)
///   → gain (compensation + trim, mute, polarity; linear ramp)
///   → 3-band EQ (low shelf, peak, high shelf biquads; bands at 0 dB are skipped)
///   → [insert slots — WP10]
///   → look-ahead limiter (always on).
///
/// Polarity is folded into the ramped gain, so flipping it ramps through
/// zero instead of clicking. prepare()/configure() allocate; everything else
/// is real-time safe.
class SpeakerChain {
public:
    static constexpr float kMaxDelayMs = 100.0f;
    static constexpr float kDelayCrossfadeMs = 50.0f;

    void prepare(double sampleRate, int maxBlockSize);

    /// Non-RT. Applies settings immediately (no ramps) and clears all state.
    void configure(const SpeakerChainSettings& settings);

    // ---- RT setters (from commands) ----
    void setTrimDb(float trimDb, float rampMs = -1.0f) noexcept;
    void setMute(bool mute, float rampMs = -1.0f) noexcept;
    void setPolarityInverted(bool inverted) noexcept;
    void setUserDelayMs(float ms) noexcept;
    void setEq(const Eq3& eq) noexcept;

    /// RT. Graph swap with state carry-over: keep this chain's signal state
    /// (delay contents, filters, limiter) and move towards `fresh`'s settings
    /// with the usual ramps / delay crossfade.
    void retargetTo(const SpeakerChain& fresh) noexcept;

    /// RT. In place.
    void process(float* samples, int numSamples) noexcept;

    int speakerId() const noexcept { return settings_.speakerId; }
    const SpeakerChainSettings& settings() const noexcept { return settings_; }
    double sampleRate() const noexcept { return sampleRate_; }
    /// Effective delay the chain is heading for, in samples (excludes the limiter).
    float targetDelaySamples() const noexcept { return delayTarget_; }
    int limiterLatencySamples() const noexcept { return limiter_.latencySamples(); }

    /// Meters since the last call; reset on read.
    float takePeak() noexcept {
        const float p = peak_;
        peak_ = 0.0f;
        return p;
    }
    float takeMinLimiterGain() noexcept { return limiter_.takeMinGain(); }

private:
    float gainTarget() const noexcept;
    float delaySamplesFor(float compensationMs, float userMs) const noexcept;
    void updateEqCoefficients() noexcept;

    using Delay = juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd>;
    using Filter = juce::dsp::IIR::Filter<float>;

    SpeakerChainSettings settings_;
    double sampleRate_{48000.0};

    Delay delay_;
    float delayCurrent_{0.0f};  // samples
    float delayTarget_{0.0f};
    float delayFrom_{0.0f};
    int xfadeLength_{1};
    int xfadePos_{-1};  // -1 = not crossfading

    GainRamp gain_;
    std::array<Filter, 3> eq_;
    std::array<bool, 3> eqActive_{};
    LookaheadLimiter limiter_;
    float peak_{0.0f};
};

}  // namespace spacer
