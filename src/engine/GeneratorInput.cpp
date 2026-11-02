#include "engine/GeneratorInput.h"

#include <algorithm>
#include <bit>
#include <cmath>

#include "core/SpecEquality.h"

namespace spacer {

GeneratorInput::GeneratorInput(const GeneratorSpec& spec) : spec_(spec) {}

void GeneratorInput::prepare(double sampleRate, int /*maxBlockSize*/) {
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    phase_ = 0.0;
    increment_ = std::max(0.0, static_cast<double>(spec_.hz)) / sampleRate_;
    impulsePeriod_ =
        std::max<std::int64_t>(1, std::llround(static_cast<double>(spec_.periodSeconds) * sampleRate_));
    impulseCounter_ = 0;
    random_.setSeed(kSeed);
    pinkSum_ = 0.0f;
    for (auto& r : pinkRows_) {
        r = nextWhite();
        pinkSum_ += r;
    }
    pinkCounter_ = 0;
}

float GeneratorInput::nextPink() noexcept {
    // Voss–McCartney: row k is redrawn every 2^(k+1) samples (the row picked
    // by the number of trailing zeros of a counter), plus a fresh white
    // sample every sample.
    ++pinkCounter_;
    const int row = std::countr_zero(pinkCounter_);
    if (row < kPinkRows) {
        const float fresh = nextWhite();
        pinkSum_ += fresh - pinkRows_[static_cast<std::size_t>(row)];
        pinkRows_[static_cast<std::size_t>(row)] = fresh;
    }
    return (pinkSum_ + nextWhite()) * (1.0f / static_cast<float>(kPinkRows + 1));
}

void GeneratorInput::read(const InputContext& /*ctx*/, float* const* dest, int numChannels, int n) noexcept {
    if (numChannels <= 0) return;
    float* out = dest[0];
    switch (spec_.type) {
        case GeneratorKind::Sine:
            for (int i = 0; i < n; ++i) {
                out[i] = static_cast<float>(std::sin(juce::MathConstants<double>::twoPi * phase_));
                phase_ += increment_;
                if (phase_ >= 1.0) phase_ -= std::floor(phase_);
            }
            break;
        case GeneratorKind::White:
            for (int i = 0; i < n; ++i) out[i] = nextWhite();
            break;
        case GeneratorKind::Pink:
            for (int i = 0; i < n; ++i) out[i] = nextPink();
            break;
        case GeneratorKind::Impulse:
            for (int i = 0; i < n; ++i) {
                out[i] = impulseCounter_ == 0 ? 1.0f : 0.0f;
                if (++impulseCounter_ >= impulsePeriod_) impulseCounter_ = 0;
            }
            break;
    }
    for (int c = 1; c < numChannels; ++c) std::copy(out, out + n, dest[c]);
}

void GeneratorInput::takeStateFrom(SourceInput& old) noexcept {
    auto* o = dynamic_cast<GeneratorInput*>(&old);
    if (o == nullptr || !specEqual(o->spec_, spec_)) return;
    phase_ = o->phase_;
    random_ = o->random_;
    pinkRows_ = o->pinkRows_;
    pinkSum_ = o->pinkSum_;
    pinkCounter_ = o->pinkCounter_;
    if (juce::exactlyEqual(o->sampleRate_, sampleRate_))
        impulseCounter_ = o->impulseCounter_ % impulsePeriod_;
}

}  // namespace spacer
