#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <random>
#include <vector>

#include "EngineTestUtils.h"
#include "engine/LookaheadLimiter.h"
#include "engine/OfflineRenderer.h"

using Catch::Approx;
using namespace spacer;
using namespace spacer::testing;

namespace {

const float kCeiling = juce::Decibels::decibelsToGain(-0.3f);

std::vector<float> run(LookaheadLimiter& lim, std::vector<float> x, int block = 128) {
    for (std::size_t i = 0; i < x.size(); i += static_cast<std::size_t>(block))
        lim.process(x.data() + i,
                    static_cast<int>(std::min<std::size_t>(static_cast<std::size_t>(block), x.size() - i)));
    return x;
}

}  // namespace

TEST_CASE("Limiter: never exceeds -0.3 dBFS on a +12 dB sine") {
    for (double sr : {44100.0, 48000.0, 96000.0}) {
        LookaheadLimiter lim;
        lim.prepare(sr);
        const auto in = sine(sr, 997.0, juce::Decibels::decibelsToGain(12.0f), static_cast<int>(sr));
        std::vector<float> x(in.getReadPointer(0), in.getReadPointer(0) + in.getNumSamples());
        const auto y = run(lim, x);
        CHECK(peakAbs(y.data(), static_cast<int>(y.size())) <= kCeiling);
        // and it actually limits rather than mutes
        CHECK(peakAbs(y.data() + y.size() / 2, static_cast<int>(y.size() / 2)) > kCeiling * 0.95f);
    }
}

TEST_CASE("Limiter: never exceeds the ceiling on noise with sudden spikes") {
    LookaheadLimiter lim;
    lim.prepare(48000.0);
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> u(-1.0f, 1.0f);
    std::vector<float> x(480000);
    for (auto& v : x) v = u(rng) * 0.5f;
    for (std::size_t i = 1000; i < x.size(); i += 777)
        x[i] = (i % 2 ? 1.0f : -1.0f) * 16.0f * std::abs(u(rng));
    const auto y = run(lim, x, 61);
    CHECK(peakAbs(y.data(), static_cast<int>(y.size())) <= kCeiling);
}

TEST_CASE("Limiter: below the ceiling it is a pure delay of 1.5 ms") {
    LookaheadLimiter lim;
    lim.prepare(48000.0);
    CHECK(lim.latencySamples() == 72);
    std::mt19937 rng(1);
    std::uniform_real_distribution<float> u(-0.9f, 0.9f);
    std::vector<float> x(10000);
    for (auto& v : x) v = u(rng);
    const auto y = run(lim, x);
    for (std::size_t i = 72; i < x.size(); ++i) REQUIRE(y[i] == x[i - 72]);
    CHECK(lim.takeMinGain() == 1.0f);
}

TEST_CASE("Limiter: releases with an 80 ms time constant after a peak") {
    LookaheadLimiter lim;
    lim.prepare(48000.0);
    std::vector<float> x(48000, 0.1f);
    x[100] = 4.0f;  // needs ~-12.3 dB
    const auto y = run(lim, x);
    const int L = lim.latencySamples();
    // Shortly after the peak the gain is still well reduced; 80 ms later it
    // has recovered by 1 - 1/e of the way; after 1 s it is fully back.
    const float atPeak = y[static_cast<std::size_t>(100 + L)] / 4.0f;
    CHECK(atPeak <= kCeiling / 4.0f + 1e-6f);
    const float g80 = y[static_cast<std::size_t>(100 + L + 3840 + L)] / 0.1f;
    const float expected = 1.0f - (1.0f - atPeak) * std::exp(-1.0f);
    CHECK(g80 == Approx(expected).margin(0.02));
    CHECK(y.back() / 0.1f == Approx(1.0f).margin(1e-4));
}

TEST_CASE("Limiter: a full render with +12 dB source gain stays under -0.3 dBFS") {
    Project p = makeProject({makeSpeaker(1, -90.0f, 2.0f, 0), makeSpeaker(2, 90.0f, 2.0f, 1)});
    SourceSpec s = makeSource(1, {}, SpatializerKind::Direct);
    s.directSpeakers = {1, 2};
    s.gainDb = 12.0f;
    p.sources = {s};
    auto r = OfflineRenderer::render(p, {{1, sine(48000.0, 440.0, 1.0f, 48000)}}, 1.0);
    REQUIRE(r.hasValue());
    for (int c = 0; c < 2; ++c) {
        CHECK(peakAbs(r->audio.getReadPointer(c), r->audio.getNumSamples()) <= kCeiling);
        CHECK(peakAbs(r->audio.getReadPointer(c, 24000), 24000) > 0.9f);
    }
}
