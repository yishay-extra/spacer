#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdio>
#include <random>
#include <vector>

#include "EngineTestUtils.h"
#include "engine/AllocationGuard.h"
#include "engine/BufferInput.h"
#include "engine/EngineProcessor.h"
#include "engine/GraphBuilder.h"

using namespace spacer;
using namespace spacer::testing;

// SPEC §17: 64 sources, 32 speakers, 128 frames at 48 kHz must process in
// < 1.0 ms. The spec's mix includes 5th-order ambisonics, which does not
// exist yet (WP2d); until then all 64 sources are VBAP with spread (MDAP,
// the most expensive VBAP path), half of them stereo, with distance filters.
TEST_CASE("Benchmark: 64 sources x 32 speakers, 128 frames at 48 kHz", "[benchmark]") {
    std::vector<Speaker> speakers;
    for (int i = 0; i < 32; ++i)
        speakers.push_back(makeSpeaker(i + 1, -180.0f + 11.25f * static_cast<float>(i) + 5.0f,
                                       2.0f + 0.05f * static_cast<float>(i % 5), i));
    Project p = makeProject(speakers);
    for (int i = 0; i < 64; ++i) {
        SourceSpec s = makeSource(i + 1, {static_cast<float>(i * 7 - 180), 0.0f, 3.0f});
        s.stereo = (i % 2) == 0;
        s.spread = 0.25f;
        s.distance.k = 1.0f;
        s.distance.q = 0.3f;
        p.sources.push_back(s);
    }
    std::mt19937 rng(3);
    std::uniform_real_distribution<float> u(-0.25f, 0.25f);
    juce::AudioBuffer<float> noise(2, 48000 * 3);
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < noise.getNumSamples(); ++i) noise.setSample(c, i, u(rng));

    GraphBuilder b(p);
    b.setInputFactory([&](const SourceSpec&) -> std::unique_ptr<SourceInput> {
        return std::make_unique<BufferInput>(noise);
    });
    EngineProcessor proc;
    proc.setGraphWhileStopped(b.build(48000.0, 128));
    juce::AudioBuffer<float> out(32, 128);
    std::vector<float*> ptrs;
    for (int c = 0; c < 32; ++c) ptrs.push_back(out.getWritePointer(c));

    allocguard::setMode(allocguard::Mode::Count);
    allocguard::resetViolations();
    const int warmup = 50, blocks = 1000;
    for (int i = 0; i < warmup; ++i) proc.process(nullptr, 0, ptrs.data(), 32, 128);
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < blocks; ++i) {
        // Keep every source moving so gains are interpolated each block.
        proc.push(cmd::SetSourcePosition{
            1 + i % 64, -1, {static_cast<float>((i * 13) % 360 - 180), 0.0f, 3.0f}, 50.0f});
        proc.process(nullptr, 0, ptrs.data(), 32, 128);
        Feedback f;
        while (proc.popFeedback(f)) {
        }
    }
    const double ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / blocks;
    std::printf("[benchmark] 64 sources x 32 speakers x 128 frames: %.3f ms per block (budget 1.0 ms)\n", ms);

    CHECK(allocguard::violations() == 0);
#ifdef NDEBUG
    CHECK(ms < 1.0);
#endif
}
