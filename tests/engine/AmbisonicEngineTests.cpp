#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <memory>
#include <vector>

#include "EngineTestUtils.h"
#include "engine/AllocationGuard.h"
#include "engine/BufferInput.h"
#include "engine/EngineProcessor.h"
#include "engine/GraphBuilder.h"
#include "engine/OfflineRenderer.h"

using Catch::Approx;
using namespace spacer;
using namespace spacer::testing;

namespace {

constexpr double kSr = 48000.0;
constexpr int kBlock = 128;
constexpr double kHz = 480.0;  // 100 samples per period

std::vector<Speaker> ring(int count, float offsetDeg, float elevation = 0.0f) {
    std::vector<Speaker> v;
    for (int i = 0; i < count; ++i) {
        Speaker s = makeSpeaker(
            i + 1, -180.0f + offsetDeg + 360.0f * static_cast<float>(i) / static_cast<float>(count), 2.0f, i);
        s.position.elevation = elevation;
        v.push_back(s);
    }
    return v;
}

Project ambisonicProject(std::vector<Speaker> speakers, Position source, int order, float spread = 0.0f) {
    SourceSpec s = makeSource(1, source, SpatializerKind::Ambisonic);
    s.spread = spread;
    Project p = makeProject(std::move(speakers), {s});
    p.ambisonics.order = order;
    return p;
}

/// Per-speaker RMS of the steady state of an offline render.
std::vector<float> speakerRms(const Project& p) {
    auto r = OfflineRenderer::render(p, {{1, sine(kSr, kHz, 0.5f, 48000)}}, 1.0);
    REQUIRE(r.hasValue());
    std::vector<float> out;
    for (int c = 0; c < r->audio.getNumChannels(); ++c)
        out.push_back(rms(r->audio.getReadPointer(c, 9600), 24000));
    return out;
}

struct Rig {
    EngineProcessor proc;
    int channels;
    juce::AudioBuffer<float> out;
    std::vector<std::vector<float>> history;  // per channel, all samples so far

    Rig(const Project& p, juce::AudioBuffer<float> input, int numChannels)
        : channels(numChannels), out(numChannels, kBlock), history(static_cast<std::size_t>(numChannels)) {
        proc.setGraphWhileStopped(build(p, input));
    }
    static std::unique_ptr<Graph> build(const Project& p, const juce::AudioBuffer<float>& input) {
        GraphBuilder b(p);
        b.setInputFactory([input](const SourceSpec&) -> std::unique_ptr<SourceInput> {
            return std::make_unique<BufferInput>(input);
        });
        return b.build(kSr, kBlock);
    }
    void run(int blocks) {
        std::vector<float*> ptrs;
        for (int c = 0; c < channels; ++c) ptrs.push_back(out.getWritePointer(c));
        for (int b = 0; b < blocks; ++b) {
            proc.process(nullptr, 0, ptrs.data(), channels, kBlock);
            for (std::size_t c = 0; c < ptrs.size(); ++c)
                history[c].insert(history[c].end(), ptrs[c], ptrs[c] + kBlock);
        }
    }
    /// Largest sample-to-sample step on any channel from sample `from` on.
    float maxStep(std::size_t from) const {
        float m = 0.0f;
        for (const auto& h : history)
            for (std::size_t i = std::max<std::size_t>(from, 1); i < h.size(); ++i)
                m = std::max(m, std::abs(h[i] - h[i - 1]));
        return m;
    }
};

// A pure tone moves by at most this much per sample at amplitude `a`.
float maxSlope(float a) {
    return a * static_cast<float>(2.0 * juce::MathConstants<double>::pi * kHz / kSr);
}

}  // namespace

TEST_CASE("Ambisonic engine: a source at a speaker direction peaks at that speaker (horizontal ring)") {
    for (int order : {1, 3, 5}) {
        for (int k : {0, 2, 5}) {
            const auto speakers = ring(8, 0.0f);
            const Project p = ambisonicProject(
                speakers, {speakers[static_cast<std::size_t>(k)].position.azimuth, 0.0f, 2.0f}, order);
            const auto r = speakerRms(p);
            INFO("order " << order << " speaker " << k);
            float best = 0.0f;
            int bestIdx = -1;
            double energy = 0.0;
            for (int i = 0; i < 8; ++i) {
                energy +=
                    static_cast<double>(r[static_cast<std::size_t>(i)]) * r[static_cast<std::size_t>(i)];
                if (r[static_cast<std::size_t>(i)] > best) {
                    best = r[static_cast<std::size_t>(i)];
                    bestIdx = i;
                }
            }
            CHECK(bestIdx == k);
            // The neighbours are lower by a clear margin at every order.
            for (int i = 0; i < 8; ++i)
                if (i != k) CHECK(r[static_cast<std::size_t>(i)] < 0.9f * best);
            // Σg² = 1 convention (SPEC §6.3): total power of a 0.5 amplitude tone is 0.125.
            // The decoder is normalised to the mean over the sphere, so an
            // in-plane source on a horizontal ring is up to ~1.2 dB louder.
            CHECK(energy == Approx(0.125).epsilon(0.3));
            CHECK(energy >= 0.125 * 0.95);
        }
    }
}

TEST_CASE("Ambisonic engine: a source at a speaker direction peaks at that speaker (3D layout)") {
    // Cube corners: two rings of four at ±35.26°.
    std::vector<Speaker> speakers;
    for (int i = 0; i < 4; ++i) {
        Speaker lo = makeSpeaker(i + 1, -135.0f + 90.0f * static_cast<float>(i), 2.0f, i);
        lo.position.elevation = -35.26f;
        Speaker hi = makeSpeaker(i + 5, -135.0f + 90.0f * static_cast<float>(i), 2.0f, i + 4);
        hi.position.elevation = 35.26f;
        speakers.push_back(lo);
        speakers.push_back(hi);
    }
    for (std::size_t k = 0; k < speakers.size(); ++k) {
        const Project p = ambisonicProject(speakers, speakers[k].position, 3);
        const auto r = speakerRms(p);
        std::size_t best = 0;
        for (std::size_t i = 0; i < r.size(); ++i)
            if (r[i] > r[best]) best = i;
        INFO("speaker " << k);
        CHECK(static_cast<int>(best) == speakers[k].outputChannel);
    }
}

TEST_CASE("Ambisonic engine: spread keeps the loudness and widens the image") {
    const auto speakers = ring(8, 0.0f);
    const Position at = speakers[1].position;
    float lastPeak = 2.0f;
    for (float spread : {0.0f, 0.5f, 1.0f}) {
        const auto r = speakerRms(ambisonicProject(speakers, at, 3, spread));
        double e = 0.0;
        float peak = 0.0f;
        for (float v : r) {
            e += static_cast<double>(v) * v;
            peak = std::max(peak, v);
        }
        // Compensated against the mean over the sphere (SPEC §6.3), not this direction.
        CHECK(e == Approx(0.125).epsilon(0.35));
        CHECK(peak < lastPeak);
        lastPeak = peak;
    }
}

TEST_CASE("Ambisonic engine: LFE speakers get no ambisonic signal") {
    auto speakers = ring(6, 0.0f);
    speakers.push_back(makeSpeaker(7, 0.0f, 2.0f, 6, /*lfe=*/true));
    const auto r = speakerRms(ambisonicProject(speakers, speakers[0].position, 3));
    CHECK(r[6] == 0.0f);
    CHECK(r[0] > 0.1f);
}

TEST_CASE("Ambisonic engine: the graph carries the project decoder, the bus order and no VBAP fallback") {
    const auto speakers = ring(8, 0.0f);
    const Project p = ambisonicProject(speakers, speakers[0].position, 4);
    GraphBuilder b(p);
    auto g = b.build(kSr, kBlock);
    CHECK(g->ambisonicOrder() == 4);
    CHECK(g->ambisonicDecoder().isPrepared());
    CHECK(g->ambisonicDecoder().order() == 4);
    CHECK(g->ambisonicDecoder().numSpeakers() == 8);
    CHECK(g->source(0).renderedPosition(0).distance >= 0.0f);
}

TEST_CASE("Ambisonic engine: an order change through SwapGraph is click-free and allocation-free") {
    const auto speakers = ring(8, 0.0f);
    const Position at{20.0f, 10.0f, 2.0f};
    const auto input = sine(kSr, kHz, 0.5f, 48000 * 3);

    for (const auto& [from, to] : std::vector<std::pair<int, int>>{{3, 5}, {5, 2}, {2, 4}}) {
        INFO("order " << from << " -> " << to);
        Rig rig(ambisonicProject(speakers, at, from), input, 8);
        rig.run(60);  // settle: fade-in, limiter latency
        const std::size_t swapAt = rig.history[0].size();
        const float before = rig.maxStep(swapAt - 20 * kBlock);
        REQUIRE(before <= maxSlope(0.5f) * 1.05f);

        REQUIRE(rig.proc.swapGraph(Rig::build(ambisonicProject(speakers, at, to), input)));
        allocguard::setMode(allocguard::Mode::Count);
        allocguard::resetViolations();
        rig.run(1);
        CHECK(rig.proc.currentGraph()->isAmbisonicCrossfading());
        rig.run(40);  // > 50 ms
        CHECK(allocguard::violations() == 0);
        CHECK_FALSE(rig.proc.currentGraph()->isAmbisonicCrossfading());

        // No step beyond what the tone itself does (a per-speaker gain is <= 1.05).
        CHECK(rig.maxStep(swapAt - kBlock) <= maxSlope(0.5f) * 1.1f);

        // And it ends exactly where a fresh engine at the new order sits.
        Rig fresh(ambisonicProject(speakers, at, to), input, 8);
        fresh.run(100);
        for (int c = 0; c < 8; ++c) {
            const auto& a = rig.history[static_cast<std::size_t>(c)];
            const auto& f = fresh.history[static_cast<std::size_t>(c)];
            // Same input phase: both engines play the same buffer from its start,
            // so compare the level, not the samples.
            CHECK(rms(a.data() + a.size() - 1600, 1600) ==
                  Approx(rms(f.data() + f.size() - 1600, 1600)).margin(2e-3));
        }
        CHECK(rig.proc.collectGarbage() == 1);
    }
}

TEST_CASE("Ambisonic engine: a layout change through SwapGraph is click-free") {
    const Position at{20.0f, 0.0f, 2.0f};
    const auto input = sine(kSr, kHz, 0.5f, 48000 * 3);
    Rig rig(ambisonicProject(ring(8, 0.0f), at, 3), input, 8);
    rig.run(60);
    const std::size_t swapAt = rig.history[0].size();
    REQUIRE(rig.proc.swapGraph(Rig::build(ambisonicProject(ring(8, 22.5f), at, 3), input)));
    rig.run(60);
    // Speaker positions moved too, so the carried chains crossfade their
    // delays; the decode itself must not add a step.
    CHECK(rig.maxStep(swapAt - kBlock) <= maxSlope(0.5f) * 1.1f);
}

TEST_CASE("Ambisonic engine: a second swap during a crossfade waits and nothing is dropped") {
    const auto speakers = ring(8, 0.0f);
    const Position at{20.0f, 0.0f, 2.0f};
    const auto input = sine(kSr, kHz, 0.5f, 48000 * 3);
    Rig rig(ambisonicProject(speakers, at, 3), input, 8);
    rig.run(30);
    const std::size_t swapAt = rig.history[0].size();
    REQUIRE(rig.proc.swapGraph(Rig::build(ambisonicProject(speakers, at, 5), input)));
    rig.run(3);
    REQUIRE(rig.proc.swapGraph(Rig::build(ambisonicProject(speakers, at, 1), input)));
    rig.run(80);
    CHECK(rig.proc.currentGraph()->ambisonicOrder() == 1);
    CHECK_FALSE(rig.proc.currentGraph()->isAmbisonicCrossfading());
    CHECK(rig.maxStep(swapAt - kBlock) <= maxSlope(0.5f) * 1.1f);
    CHECK(rig.proc.collectGarbage() == 2);
}

TEST_CASE("Ambisonic engine: a swap with an unchanged decoder does not crossfade") {
    const auto speakers = ring(8, 0.0f);
    const Position at{20.0f, 0.0f, 2.0f};
    const auto input = sine(kSr, kHz, 0.5f, 48000 * 3);
    Rig rig(ambisonicProject(speakers, at, 3), input, 8);
    rig.run(10);
    Project next = ambisonicProject(speakers, at, 3);
    next.sources[0].gainDb = -3.0f;
    REQUIRE(rig.proc.swapGraph(Rig::build(next, input)));
    rig.run(1);
    CHECK_FALSE(rig.proc.currentGraph()->isAmbisonicCrossfading());
}
