#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "EngineTestUtils.h"
#include "engine/SpeakerChain.h"

using Catch::Approx;
using namespace spacer;
using namespace spacer::testing;

namespace {

constexpr double kSr = 48000.0;

SpeakerChain makeChain(const SpeakerChainSettings& s) {
    SpeakerChain c;
    c.prepare(kSr, 128);
    c.configure(s);
    return c;
}

std::vector<float> run(SpeakerChain& c, std::vector<float> x) {
    for (std::size_t i = 0; i < x.size(); i += 128)
        c.process(x.data() + i, static_cast<int>(std::min<std::size_t>(128, x.size() - i)));
    return x;
}

std::vector<float> impulseAt(std::size_t at, std::size_t length, float v = 0.5f) {
    std::vector<float> x(length, 0.0f);
    x[at] = v;
    return x;
}

}  // namespace

TEST_CASE("SpeakerChain: integer delay = compensation + user delay + limiter latency") {
    SpeakerChainSettings s;
    s.compensationDelayMs = 2.0f;  // 96 samples
    s.userDelayMs = 0.5f;          // 24 samples
    auto c = makeChain(s);
    const auto y = run(c, impulseAt(10, 1024));
    const std::size_t expected = 10 + 120 + static_cast<std::size_t>(c.limiterLatencySamples());
    CHECK(y[expected] == Approx(0.5f));
    float others = 0.0f;
    for (std::size_t i = 0; i < y.size(); ++i)
        if (i != expected) others += std::abs(y[i]);
    CHECK(others < 1e-6f);
}

TEST_CASE("SpeakerChain: delay is clamped to 100 ms") {
    SpeakerChainSettings s;
    s.compensationDelayMs = 80.0f;
    s.userDelayMs = 80.0f;
    auto c = makeChain(s);
    CHECK(c.targetDelaySamples() == Approx(4800.0f));
}

TEST_CASE("SpeakerChain: gain = compensation + trim, polarity inverts") {
    SpeakerChainSettings s;
    s.compensationGainDb = -3.0f;
    s.trimDb = -3.0f;
    s.polarityInverted = true;
    auto c = makeChain(s);
    const auto y = run(c, impulseAt(0, 512));
    CHECK(y[static_cast<std::size_t>(c.limiterLatencySamples())] ==
          Approx(-0.5f * juce::Decibels::decibelsToGain(-6.0f)).margin(1e-5));
}

TEST_CASE("SpeakerChain: mute ramps to silence over 10 ms") {
    auto c = makeChain({});
    std::vector<float> x(4800, 0.5f);
    c.process(x.data(), 128);  // settle
    c.setMute(true);
    std::vector<float> y(x.begin() + 128, x.end());
    y = run(c, y);
    const int L = c.limiterLatencySamples();
    // Ramp starts at the chain's gain stage, then passes the limiter delay.
    CHECK(y[static_cast<std::size_t>(L + 1)] > 0.45f);
    CHECK(y[static_cast<std::size_t>(L + 240)] == Approx(0.25f).margin(0.01));
    CHECK(y[static_cast<std::size_t>(L + 480)] == 0.0f);
    CHECK(y.back() == 0.0f);
}

TEST_CASE("SpeakerChain: delay change crossfades over 50 ms without a jump") {
    auto c = makeChain({});
    const auto in = sine(kSr, 200.0, 0.5f, 48000);
    std::vector<float> x(in.getReadPointer(0), in.getReadPointer(0) + in.getNumSamples());
    std::vector<float> y(x.size());
    std::size_t pos = 0;
    const auto block = [&](std::size_t n) {
        std::copy(x.begin() + static_cast<long>(pos), x.begin() + static_cast<long>(pos + n),
                  y.begin() + static_cast<long>(pos));
        c.process(y.data() + pos, static_cast<int>(n));
        pos += n;
    };
    for (int i = 0; i < 40; ++i) block(128);
    c.setUserDelayMs(10.0f);  // 480 samples
    while (pos + 128 <= x.size()) block(128);

    // Largest sample-to-sample step stays that of the sine itself (no click).
    float maxStep = 0.0f;
    for (std::size_t i = 1; i < pos; ++i) maxStep = std::max(maxStep, std::abs(y[i] - y[i - 1]));
    const float sineStep = 0.5f * 2.0f * juce::MathConstants<float>::pi * 200.0f / 48000.0f;
    CHECK(maxStep <= sineStep * 1.05f);

    // After the crossfade the output is the input delayed by 480 + L samples.
    const auto L = static_cast<std::size_t>(c.limiterLatencySamples());
    for (std::size_t i = 40 * 128 + 2400 + 600; i < pos; i += 97)
        REQUIRE(y[i] == Approx(x[i - 480 - L]).margin(1e-4));
}

TEST_CASE("SpeakerChain: low-shelf EQ boosts lows by its gain and leaves highs") {
    SpeakerChainSettings s;
    s.eq.lowFreq = 200.0f;
    s.eq.lowGainDb = 6.0f;
    auto c = makeChain(s);
    const auto lo = sine(kSr, 40.0, 0.1f, 48000);
    auto ylo = run(c, std::vector<float>(lo.getReadPointer(0), lo.getReadPointer(0) + 48000));
    CHECK(20.0f * std::log10(rms(ylo.data() + 24000, 24000) / rms(lo.getReadPointer(0, 24000), 24000)) ==
          Approx(6.0f).margin(0.3));

    auto c2 = makeChain(s);
    const auto hi = sine(kSr, 5000.0, 0.1f, 48000);
    auto yhi = run(c2, std::vector<float>(hi.getReadPointer(0), hi.getReadPointer(0) + 48000));
    CHECK(20.0f * std::log10(rms(yhi.data() + 24000, 24000) / rms(hi.getReadPointer(0, 24000), 24000)) ==
          Approx(0.0f).margin(0.2));
}

TEST_CASE("SpeakerChain: retargetTo keeps the signal state and ramps to the new settings") {
    auto running = makeChain({});
    std::vector<float> x(256, 0.5f);
    running.process(x.data(), 256);

    SpeakerChainSettings next;
    next.trimDb = -6.0f;
    auto fresh = makeChain(next);
    running.retargetTo(fresh);
    CHECK(running.settings().trimDb == -6.0f);

    std::vector<float> y(4800, 0.5f);
    y = run(running, y);
    // Delay line contents carried: no gap of silence at the start.
    CHECK(y.front() > 0.45f);
    CHECK(y.back() == Approx(0.5f * juce::Decibels::decibelsToGain(-6.0f)).margin(1e-4));
}
