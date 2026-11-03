#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <cstdint>

#include "core/Source.h"
#include "engine/SourceInput.h"

namespace spacer {

/// Test-signal source (SPEC §8 `Generator`), for layout testing. Mono; a
/// stereo source gets the same signal on both channels. Levels are full
/// scale, the source's gainDb sets the level:
///   - sine{hz}: peak 1, phase 0 at the first sample;
///   - white: uniform in [−1, 1];
///   - pink: Voss–McCartney (16 octave rows + one white row), peak ≤ 1
///     (about −17 dBFS RMS), −3 dB/octave from ~1 Hz to Nyquist;
///   - impulse{periodSeconds}: a single sample of 1.0 at the first sample and
///     then every round(periodSeconds · sampleRate) samples.
/// Noise is seeded with a fixed seed, so renders are deterministic.
class GeneratorInput final : public SourceInput {
public:
    static constexpr int kPinkRows = 16;
    static constexpr std::int64_t kSeed = 0x5eed;

    explicit GeneratorInput(const GeneratorSpec& spec);

    void prepare(double sampleRate, int maxBlockSize) override;
    void read(const InputContext& ctx, float* const* dest, int numChannels, int numSamples) noexcept override;
    /// Same generator (type and parameters): continue its phase / noise state.
    void takeStateFrom(SourceInput& old) noexcept override;

    const GeneratorSpec& spec() const noexcept { return spec_; }

private:
    float nextWhite() noexcept { return random_.nextFloat() * 2.0f - 1.0f; }
    float nextPink() noexcept;

    GeneratorSpec spec_;
    double sampleRate_{48000.0};
    double phase_{0.0};      // sine, cycles in [0, 1)
    double increment_{0.0};  // sine, cycles per sample
    std::int64_t impulsePeriod_{48000};
    std::int64_t impulseCounter_{0};
    juce::Random random_{kSeed};
    std::array<float, kPinkRows> pinkRows_{};
    float pinkSum_{0.0f};
    std::uint32_t pinkCounter_{0};
};

}  // namespace spacer
