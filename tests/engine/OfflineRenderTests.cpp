#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

#include "EngineTestUtils.h"
#include "core/Layout.h"
#include "engine/OfflineRenderer.h"

using Catch::Approx;
using namespace spacer;
using namespace spacer::testing;

namespace {
constexpr double kSr = 48000.0;
}

TEST_CASE("Offline render: a tone at az -90 on a ±90 pair appears only on the left channel") {
    const Project p = makeProject({makeSpeaker(1, -90.0f, 2.0f, 0), makeSpeaker(2, 90.0f, 2.0f, 1)},
                                  {makeSource(1, {-90.0f, 0.0f, 2.0f})});
    auto r = OfflineRenderer::render(p, {{1, sine(kSr, 440.0, 0.25f, 48000)}}, 1.0);
    REQUIRE(r.hasValue());
    const auto& a = r->audio;
    REQUIRE(a.getNumChannels() == 2);
    // Skip the initial fade-in (10 ms) and look at the steady state.
    const int start = 4800, n = 38400;
    CHECK(rms(a.getReadPointer(0, start), n) == Approx(0.25f / std::sqrt(2.0f)).epsilon(0.01));
    CHECK(peakAbs(a.getReadPointer(1), a.getNumSamples()) == 0.0f);
}

TEST_CASE("Offline render: a tone at az +90 appears only on the right channel") {
    const Project p = makeProject({makeSpeaker(1, -90.0f, 2.0f, 0), makeSpeaker(2, 90.0f, 2.0f, 1)},
                                  {makeSource(1, {90.0f, 0.0f, 2.0f})});
    auto r = OfflineRenderer::render(p, {{1, sine(kSr, 440.0, 0.25f, 48000)}}, 1.0);
    REQUIRE(r.hasValue());
    CHECK(peakAbs(r->audio.getReadPointer(0), r->audio.getNumSamples()) < 1e-6f);  // -120 dB
    CHECK(rms(r->audio.getReadPointer(1, 4800), 38400) > 0.1f);
}

TEST_CASE("Offline render: compensation delay and gain match computeCompensation") {
    // Speaker 1 is 1 m nearer: delayed by 1/343 s (≈139.9 samples, fractional)
    // and attenuated by 20·log10(2/3) dB. Both get the same limiter latency.
    Project p = makeProject({makeSpeaker(1, -30.0f, 2.0f, 0), makeSpeaker(2, 30.0f, 3.0f, 1)});
    SourceSpec s = makeSource(1, {}, SpatializerKind::Direct);
    s.directSpeakers = {1, 2};
    p.sources = {s};

    const double hz = 187.5;  // 256 samples per period at 48 kHz, longer than any delay here
    auto r = OfflineRenderer::render(p, {{1, sine(kSr, hz, 0.1f, 48000)}}, 1.0);
    REQUIRE(r.hasValue());
    const auto comp = computeCompensation(p.layout);
    const double w = 2.0 * juce::MathConstants<double>::pi * hz / kSr;

    const int start = 9600, n = 256 * 100;
    const auto delayOf = [&](int ch) {
        double ph = -phaseOf(r->audio.getReadPointer(ch), start, n, hz, kSr);
        while (ph < 0.0) ph += 2.0 * juce::MathConstants<double>::pi;
        return ph / w;  // modulo one period (256 samples)
    };
    const double expected0 = comp[0].delayMs * 0.001 * kSr + r->latencySamples;
    const double expected1 = comp[1].delayMs * 0.001 * kSr + r->latencySamples;
    CHECK(comp[1].delayMs == 0.0f);
    CHECK(delayOf(0) == Approx(std::fmod(expected0, 256.0)).margin(0.02));
    CHECK(delayOf(1) == Approx(std::fmod(expected1, 256.0)).margin(0.02));

    // The delay difference, unwrapped: the near speaker is delayed by the
    // compensation (1/343 s).
    double diff = delayOf(0) - delayOf(1);
    while (diff < 0.0) diff += 256.0;
    CHECK(diff == Approx(comp[0].delayMs * 0.001 * kSr).margin(0.02));

    // And the gain ratio.
    const float r0 = rms(r->audio.getReadPointer(0, start), n);
    const float r1 = rms(r->audio.getReadPointer(1, start), n);
    CHECK(20.0f * std::log10(r0 / r1) == Approx(comp[0].gainDb - comp[1].gainDb).margin(0.01));

    // Absolute timing of the onset on the far speaker: exactly the limiter latency.
    const float* far = r->audio.getReadPointer(1);
    int first = -1;
    for (int i = 0; i < r->audio.getNumSamples() && first < 0; ++i)
        if (far[i] != 0.0f) first = i;
    CHECK(first == r->latencySamples + 1);  // sample 0 of a sine is 0
}

TEST_CASE("Offline render: user delay and trim add to the compensation") {
    Project p = makeProject({makeSpeaker(1, -30.0f, 2.0f, 0), makeSpeaker(2, 30.0f, 2.0f, 1)});
    p.layout.speakers[0].userDelayMs = 1.0f;  // 48 samples
    p.layout.speakers[0].trimDb = -6.0f;
    SourceSpec s = makeSource(1, {}, SpatializerKind::Direct);
    s.directSpeakers = {1, 2};
    p.sources = {s};
    // The impulse comes after the 10 ms fade-in of a new source.
    juce::AudioBuffer<float> in(1, 2000);
    in.clear();
    in.setSample(0, 1000, 0.5f);
    auto r = OfflineRenderer::render(p, {{1, in}}, 0.1);
    REQUIRE(r.hasValue());
    const int L = r->latencySamples;
    CHECK(r->audio.getSample(1, 1000 + L) == Approx(0.5f));
    CHECK(r->audio.getSample(0, 1000 + L + 48) ==
          Approx(0.5f * juce::Decibels::decibelsToGain(-6.0f)).margin(1e-4));
}

TEST_CASE("Offline render: a speaker chain spec overrides the layout speaker's trim, delay and polarity") {
    Project p = makeProject({makeSpeaker(1, -30.0f, 2.0f, 0), makeSpeaker(2, 30.0f, 2.0f, 1)});
    p.layout.speakers[0].trimDb = -20.0f;
    SpeakerChainSpec chain;
    chain.speakerId = 1;
    chain.trimDb = 0.0f;
    chain.polarityInverted = true;
    p.speakerChains = {chain};
    SourceSpec s = makeSource(1, {}, SpatializerKind::Direct);
    s.directSpeakers = {1};
    p.sources = {s};
    juce::AudioBuffer<float> in(1, 2000);
    in.clear();
    in.setSample(0, 1000, 0.5f);
    auto r = OfflineRenderer::render(p, {{1, in}}, 0.1);
    REQUIRE(r.hasValue());
    CHECK(r->audio.getSample(0, 1000 + r->latencySamples) == Approx(-0.5f));
}

TEST_CASE("Offline render: LFE speakers get no panned signal but do get Direct signal and compensation") {
    // Main pair at 3 m, LFE at 2 m in the same direction as the left speaker.
    Project p = makeProject({makeSpeaker(1, -90.0f, 3.0f, 0), makeSpeaker(2, 90.0f, 3.0f, 1),
                             makeSpeaker(3, -90.0f, 2.0f, 2, /*lfe*/ true)});
    SourceSpec panned = makeSource(1, {-90.0f, 0.0f, 3.0f});
    SourceSpec direct = makeSource(2, {}, SpatializerKind::Direct);
    direct.directSpeakers = {3};

    SECTION("panned source never reaches the LFE") {
        p.sources = {panned};
        auto r = OfflineRenderer::render(p, {{1, sine(kSr, 100.0, 0.25f, 24000)}}, 0.5);
        REQUIRE(r.hasValue());
        CHECK(peakAbs(r->audio.getReadPointer(2), r->audio.getNumSamples()) == 0.0f);
        CHECK(rms(r->audio.getReadPointer(0, 4800), 9600) > 0.1f);
    }
    SECTION("direct source reaches it, delayed and attenuated by the compensation") {
        p.sources = {direct};
        juce::AudioBuffer<float> in(1, 2000);
        in.clear();
        in.setSample(0, 1000, 0.5f);
        auto r = OfflineRenderer::render(p, {{2, in}}, 0.1);
        REQUIRE(r.hasValue());
        const auto comp = computeCompensation(p.layout);
        const double delay = comp[2].delayMs * 0.001 * kSr + r->latencySamples;  // ≈139.9 + L
        const auto* lfe = r->audio.getReadPointer(2);
        int peakAt = 0;
        for (int i = 0; i < r->audio.getNumSamples(); ++i)
            if (std::abs(lfe[i]) > std::abs(lfe[peakAt])) peakAt = i;
        CHECK(std::abs(peakAt - (1000 + delay)) < 1.0);
        float energy = 0.0f;
        for (int i = 0; i < r->audio.getNumSamples(); ++i) energy += lfe[i];  // Lagrange taps sum to 1
        CHECK(energy == Approx(0.5f * juce::Decibels::decibelsToGain(comp[2].gainDb)).margin(1e-3));
    }
}

TEST_CASE("Offline render: renders from WAV files and writes a WAV") {
    const auto dir = juce::File::createTempFile("spacer-offline");
    REQUIRE(dir.createDirectory());
    const auto inFile = dir.getChildFile("in.wav");
    const auto outFile = dir.getChildFile("out.wav");

    OfflineRenderer::Result tone;
    tone.sampleRate = kSr;
    tone.audio = sine(kSr, 1000.0, 0.5f, 4800);
    REQUIRE_FALSE(OfflineRenderer::writeWav(tone, inFile).has_value());

    const Project p = makeProject({makeSpeaker(1, -90.0f, 2.0f, 0), makeSpeaker(2, 90.0f, 2.0f, 1)},
                                  {makeSource(7, {-90.0f, 0.0f, 2.0f})});
    auto r = OfflineRenderer::renderFiles(p, {{7, inFile}}, 0.1);
    REQUIRE(r.hasValue());
    CHECK(rms(r->audio.getReadPointer(0, 1000), 2400) == Approx(0.5f / std::sqrt(2.0f)).epsilon(0.01));
    REQUIRE_FALSE(OfflineRenderer::writeWav(*r, outFile).has_value());
    CHECK(outFile.getSize() > 4800 * 2 * 4);

    SECTION("a file at another rate is resampled to the render rate") {
        auto other = OfflineRenderer::renderFiles(p, {{7, inFile}}, 0.1, {44100.0, 128, {}});
        REQUIRE(other.hasValue());
        CHECK(other->sampleRate == 44100.0);
        CHECK(rms(other->audio.getReadPointer(0, 1000), 2205) ==
              Approx(0.5f / std::sqrt(2.0f)).epsilon(0.01));
    }
    SECTION("an unreadable file is an error") {
        CHECK_FALSE(OfflineRenderer::renderFiles(p, {{7, dir.getChildFile("missing.wav")}}, 0.1).hasValue());
    }
    dir.deleteRecursively();
}

TEST_CASE("Offline render: a project without output channels is an error") {
    Project p = makeProject({makeSpeaker(1, -30.0f, 2.0f, -1), makeSpeaker(2, 30.0f, 2.0f, -1)});
    CHECK_FALSE(OfflineRenderer::render(p, {}, 0.1).hasValue());
}

TEST_CASE("Offline render: DBAP sources render through the Dbap spatializer") {
    Project p = makeProject({makeSpeaker(1, -90.0f, 2.0f, 0), makeSpeaker(2, 90.0f, 2.0f, 1)},
                            {makeSource(1, {-90.0f, 0.0f, 2.0f}, SpatializerKind::Dbap)});
    auto r = OfflineRenderer::render(p, {{1, sine(kSr, 440.0, 0.25f, 48000)}}, 1.0);
    REQUIRE(r.hasValue());
    const float left = rms(r->audio.getReadPointer(0, 4800), 38400);
    const float right = rms(r->audio.getReadPointer(1, 4800), 38400);
    // DBAP never fully silences the far speaker, but the near one dominates
    // and power is preserved (Σg² = 1).
    CHECK(left > right * 2.0f);
    CHECK(right > 0.0f);
    CHECK(left * left + right * right == Approx(0.25f * 0.25f / 2.0f).epsilon(0.01));
}
