#include "engine/SpeakerChain.h"

#include <algorithm>
#include <cmath>

namespace spacer {

namespace {
constexpr float kShelfQ = 0.70710678f;
}

void SpeakerChain::prepare(double sampleRate, int maxBlockSize) {
    sampleRate_ = sampleRate;
    const auto maxDelay = static_cast<int>(std::ceil(kMaxDelayMs * 0.001 * sampleRate)) + 4;
    delay_.prepare({sampleRate, static_cast<juce::uint32>(std::max(1, maxBlockSize)), 1});
    delay_.setMaximumDelayInSamples(maxDelay);
    xfadeLength_ = std::max(1, static_cast<int>(std::lround(kDelayCrossfadeMs * 0.001 * sampleRate)));

    for (auto& f : eq_) {
        // Give the filters biquad storage now so later coefficient updates
        // (array assignment) never allocate on the audio thread.
        f.coefficients = new juce::dsp::IIR::Coefficients<float>(1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
        *f.coefficients =
            juce::dsp::IIR::ArrayCoefficients<float>::makePeakFilter(sampleRate, 1000.0f, 1.0f, 1.0f);
        f.prepare({sampleRate, static_cast<juce::uint32>(std::max(1, maxBlockSize)), 1});
    }
    limiter_.prepare(sampleRate);
    gain_.prepare(sampleRate, 0.0f);
    if (!inserts_) inserts_ = std::make_unique<InsertStage>();
    inserts_->prepare(sampleRate, maxBlockSize, 1);
}

void SpeakerChain::configure(const SpeakerChainSettings& settings) {
    settings_ = settings;
    delay_.reset();
    delayTarget_ = delaySamplesFor(settings_.compensationDelayMs, settings_.userDelayMs);
    delayCurrent_ = delayFrom_ = delayTarget_;
    xfadePos_ = -1;
    gain_.snapTo(gainTarget());
    updateEqCoefficients();
    for (auto& f : eq_) f.reset();
    limiter_.reset();
    peak_ = 0.0f;
}

float SpeakerChain::gainTarget() const noexcept {
    if (settings_.mute) return 0.0f;
    const float g = juce::Decibels::decibelsToGain(settings_.compensationGainDb + settings_.trimDb, -200.0f);
    return settings_.polarityInverted ? -g : g;
}

float SpeakerChain::delaySamplesFor(float compensationMs, float userMs) const noexcept {
    const float ms = std::clamp(compensationMs + userMs, 0.0f, kMaxDelayMs);
    return static_cast<float>(ms * 0.001 * sampleRate_);
}

void SpeakerChain::setTrimDb(float trimDb, float rampMs) noexcept {
    settings_.trimDb = trimDb;
    gain_.setTarget(gainTarget(), rampMs);
}

void SpeakerChain::setMute(bool mute, float rampMs) noexcept {
    settings_.mute = mute;
    gain_.setTarget(gainTarget(), rampMs);
}

void SpeakerChain::setPolarityInverted(bool inverted) noexcept {
    settings_.polarityInverted = inverted;
    gain_.setTarget(gainTarget());
}

void SpeakerChain::setUserDelayMs(float ms) noexcept {
    settings_.userDelayMs = ms;
    delayTarget_ = delaySamplesFor(settings_.compensationDelayMs, settings_.userDelayMs);
}

void SpeakerChain::setEq(const Eq3& eq) noexcept {
    settings_.eq = eq;
    updateEqCoefficients();
}

void SpeakerChain::updateEqCoefficients() noexcept {
    using AC = juce::dsp::IIR::ArrayCoefficients<float>;
    const auto& e = settings_.eq;
    const float nyquistGuard = static_cast<float>(sampleRate_ * 0.45);
    const auto freq = [&](float f) { return std::clamp(f, 10.0f, nyquistGuard); };
    const float gains[3] = {e.lowGainDb, e.midGainDb, e.highGainDb};
    for (std::size_t b = 0; b < 3; ++b) {
        const bool active = !juce::exactlyEqual(gains[b], 0.0f);
        if (active && !eqActive_[b]) eq_[b].reset();  // start clean when a band is switched on
        eqActive_[b] = active;
    }
    if (eqActive_[0])
        *eq_[0].coefficients = AC::makeLowShelf(sampleRate_, freq(e.lowFreq), kShelfQ,
                                                juce::Decibels::decibelsToGain(e.lowGainDb));
    if (eqActive_[1])
        *eq_[1].coefficients = AC::makePeakFilter(sampleRate_, freq(e.midFreq), std::max(e.midQ, 0.05f),
                                                  juce::Decibels::decibelsToGain(e.midGainDb));
    if (eqActive_[2])
        *eq_[2].coefficients = AC::makeHighShelf(sampleRate_, freq(e.highFreq), kShelfQ,
                                                 juce::Decibels::decibelsToGain(e.highGainDb));
}

void SpeakerChain::retargetTo(SpeakerChain& fresh) noexcept {
    const auto& s = fresh.settings_;
    settings_.speakerId = s.speakerId;
    settings_.compensationDelayMs = s.compensationDelayMs;
    settings_.compensationGainDb = s.compensationGainDb;
    settings_.userDelayMs = s.userDelayMs;
    settings_.trimDb = s.trimDb;
    settings_.polarityInverted = s.polarityInverted;
    settings_.mute = s.mute;
    delayTarget_ = delaySamplesFor(s.compensationDelayMs, s.userDelayMs);
    gain_.setTarget(gainTarget());
    setEq(s.eq);
    if (inserts_ && fresh.inserts_) inserts_->retargetTo(*fresh.inserts_);
}

void SpeakerChain::process(float* x, int n) noexcept {
    // ---- delay, crossfading between old and new delay on change ----
    for (int i = 0; i < n; ++i) {
        if (xfadePos_ < 0 && !juce::exactlyEqual(delayTarget_, delayCurrent_)) {
            delayFrom_ = delayCurrent_;
            delayCurrent_ = delayTarget_;
            xfadePos_ = 0;
        }
        delay_.pushSample(0, x[i]);
        if (xfadePos_ >= 0) {
            const float t = static_cast<float>(xfadePos_) / static_cast<float>(xfadeLength_);
            const float a = delay_.popSample(0, delayFrom_, false);
            const float b = delay_.popSample(0, delayCurrent_, true);
            x[i] = a + (b - a) * t;
            if (++xfadePos_ >= xfadeLength_) xfadePos_ = -1;
        } else {
            x[i] = delay_.popSample(0, delayCurrent_, true);
        }
    }

    // ---- gain (compensation + trim, mute, polarity) ----
    gain_.apply(x, n);

    // ---- 3-band EQ ----
    float* channels[] = {x};
    juce::dsp::AudioBlock<float> block(channels, 1, static_cast<std::size_t>(n));
    juce::dsp::ProcessContextReplacing<float> ctx(block);
    for (std::size_t b = 0; b < eq_.size(); ++b)
        if (eqActive_[b]) eq_[b].process(ctx);

    // ---- insert slots ----
    inserts_->process(channels, 1, n);

    // ---- limiter (always on) ----
    limiter_.process(x, n);

    float p = peak_;
    for (int i = 0; i < n; ++i) p = std::max(p, std::abs(x[i]));
    peak_ = p;
}

}  // namespace spacer
