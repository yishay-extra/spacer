#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <memory>
#include <vector>

#include "EngineTestUtils.h"
#include "TestPlugins.h"
#include "engine/AllocationGuard.h"
#include "engine/InsertStage.h"

using Catch::Approx;
using namespace spacer;
using namespace spacer::testing;

namespace {

constexpr double kSr = 48000.0;
constexpr int kBlock = 64;

std::shared_ptr<HostedPlugin> makePlugin(const juce::String& kind, int channels, int delay = 0) {
    auto p = std::make_unique<TestPlugin>(kind, delay, kind == "swap");
    return std::make_shared<HostedPlugin>(std::move(p), ("SpacerTest:" + kind).toStdString(), channels, kSr,
                                          kBlock);
}

TestPlugin& testPlugin(HostedPlugin& p) {
    return dynamic_cast<TestPlugin&>(p.instance());
}

/// Runs `stage` over `in` (mono or stereo, same length) in kBlock chunks.
std::vector<std::vector<float>> run(InsertStage& stage, std::vector<std::vector<float>> in) {
    const int channels = static_cast<int>(in.size());
    const int total = static_cast<int>(in[0].size());
    for (int pos = 0; pos < total; pos += kBlock) {
        const int n = std::min(kBlock, total - pos);
        float* ch[2] = {in[0].data() + pos, in[static_cast<std::size_t>(channels - 1)].data() + pos};
        stage.process(ch, channels, n);
    }
    return in;
}

std::vector<float> impulse(int length, int at) {
    std::vector<float> x(static_cast<std::size_t>(length), 0.0f);
    x[static_cast<std::size_t>(at)] = 1.0f;
    return x;
}

std::vector<float> sineVec(int length, double hz) {
    std::vector<float> x(static_cast<std::size_t>(length));
    for (int i = 0; i < length; ++i)
        x[static_cast<std::size_t>(i)] =
            static_cast<float>(std::sin(2.0 * juce::MathConstants<double>::pi * hz * i / kSr));
    return x;
}

int peakIndex(const std::vector<float>& x) {
    int best = 0;
    for (int i = 1; i < static_cast<int>(x.size()); ++i)
        if (std::abs(x[static_cast<std::size_t>(i)]) > std::abs(x[static_cast<std::size_t>(best)])) best = i;
    return best;
}

float maxStep(const std::vector<float>& x) {
    float m = 0.0f;
    for (std::size_t i = 1; i < x.size(); ++i) m = std::max(m, std::abs(x[i] - x[i - 1]));
    return m;
}

std::unique_ptr<InsertStage> stageWith(std::vector<std::shared_ptr<HostedPlugin>> plugins, int channels,
                                       std::vector<bool> bypassed = {}) {
    auto chain = std::make_unique<InsertChain>();
    for (std::size_t i = 0; i < plugins.size(); ++i)
        chain->addSlot(plugins[i], plugins[i] ? plugins[i]->pluginId() : "missing",
                       i < bypassed.size() && bypassed[i]);
    auto stage = std::make_unique<InsertStage>(std::move(chain));
    stage->prepare(kSr, kBlock, channels);
    return stage;
}

}  // namespace

TEST_CASE("InsertChain: an empty stage passes audio through untouched") {
    InsertStage stage;
    stage.prepare(kSr, kBlock, 2);
    const auto in = sineVec(1000, 440.0);
    const auto out = run(stage, {in, in});
    CHECK(out[0] == in);
    CHECK(out[1] == in);
    CHECK(stage.pluginLatency() == 0);
}

TEST_CASE("InsertChain: plugins run in series on mono and stereo signals") {
    auto a = makePlugin("gain", 2);
    auto b = makePlugin("gain", 2);
    a->setParameter(0, 0.25f);  // x0.5
    b->setParameter(0, 0.75f);  // x1.5
    auto stage = stageWith({a, b}, 2);
    const auto in = sineVec(512, 100.0);
    const auto out = run(*stage, {in, in});
    for (std::size_t i = 0; i < in.size(); ++i) {
        CHECK(out[0][i] == Approx(in[i] * 0.75f).margin(1e-6));
        CHECK(out[1][i] == Approx(in[i] * 0.75f).margin(1e-6));
    }

    auto mono = stageWith({makePlugin("gain", 1)}, 1);
    mono->chain().setParameter(0, 0, 0.1f);
    const auto m = run(*mono, {in});
    CHECK(m[0][100] == Approx(in[100] * 0.2f).margin(1e-6));
}

TEST_CASE("InsertChain: a missing plugin slot passes through with no latency") {
    auto stage = stageWith({nullptr, makePlugin("gain", 1)}, 1);
    CHECK(stage->chain().isMissing(0));
    CHECK_FALSE(stage->chain().isMissing(1));
    CHECK(stage->pluginLatency() == 0);
    const auto in = sineVec(300, 300.0);
    CHECK(run(*stage, {in})[0] == in);  // gain plugin at its default (unity)
}

TEST_CASE("InsertChain: plugin latency is reported and padding aligns chains") {
    auto delayed = stageWith({makePlugin("delay", 1, 37)}, 1);
    CHECK(delayed->pluginLatency() == 37);
    auto plain = stageWith({}, 1);
    plain->configurePad(37, InsertChain::kMinPadCapacity);
    delayed->configurePad(0, InsertChain::kMinPadCapacity);

    const auto in = impulse(400, 10);
    CHECK(peakIndex(run(*delayed, {in})[0]) == 47);
    CHECK(peakIndex(run(*plain, {in})[0]) == 47);
}

TEST_CASE("InsertChain: bypass keeps the latency and fades without a click") {
    auto stage = stageWith({makePlugin("delay", 1, 20)}, 1);
    stage->chain().setParameter(0, 0, 0.25f);  // plugin path at half level

    // Fully bypassed: the input, delayed by the plugin's latency, at full level.
    stage->chain().setBypassed(0, true);
    run(*stage, {std::vector<float>(4096, 0.0f)});  // let the fade finish
    const auto out = run(*stage, {impulse(400, 10)});
    CHECK(peakIndex(out[0]) == 30);
    CHECK(out[0][30] == Approx(1.0f));

    // The switch itself on a sine: no sample-to-sample jump beyond the sine's own.
    auto live = stageWith({makePlugin("delay", 1, 20)}, 1);
    const auto sine = sineVec(4800, 200.0);
    std::vector<float> first(sine.begin(), sine.begin() + 2400), second(sine.begin() + 2400, sine.end());
    auto a = run(*live, {first});
    live->chain().setBypassed(0, true);
    auto b = run(*live, {second});
    a[0].insert(a[0].end(), b[0].begin(), b[0].end());
    const float sineStep = static_cast<float>(2.0 * juce::MathConstants<double>::pi * 200.0 / kSr);
    CHECK(maxStep(a[0]) <= sineStep * 1.01f);
    CHECK(live->chain().isBypassed(0));
}

TEST_CASE("InsertChain: a mono signal through a stereo-only plugin") {
    auto stage = stageWith({makePlugin("swap", 1)}, 1);
    const auto in = sineVec(256, 500.0);
    // Dual mono in, so the swapped left output is still the input.
    CHECK(run(*stage, {in})[0] == in);
}

TEST_CASE("InsertChain: a shared instance is processed only by the chain that claimed it") {
    auto shared = makePlugin("gain", 1);
    auto first = stageWith({shared}, 1);
    auto second = stageWith({shared}, 1);
    shared->setParameter(0, 0.25f);  // x0.5
    first->claim();
    const auto in = sineVec(256, 300.0);
    const int before = testPlugin(*shared).processedBlocks();
    const auto a = run(*first, {in});
    const auto b = run(*second, {in});
    CHECK(testPlugin(*shared).processedBlocks() - before == 256 / kBlock);
    CHECK(a[0][50] == Approx(0.5f * in[50]));
    CHECK(b[0] == in);  // not the owner: latency-aligned pass-through
}

TEST_CASE("InsertStage: retargeting to the same instances keeps the running chain") {
    auto p = makePlugin("delay", 1, 16);
    auto running = stageWith({p}, 1);
    auto fresh = stageWith({p}, 1, {true});
    running->configurePad(0, InsertChain::kMinPadCapacity);
    fresh->configurePad(0, InsertChain::kMinPadCapacity);
    InsertChain* chain = &running->chain();
    running->retargetTo(*fresh);
    CHECK(&running->chain() == chain);
    CHECK_FALSE(running->crossfading());
    CHECK(running->chain().isBypassed(0));  // settings adopted
}

TEST_CASE("InsertStage: retargeting to other plugins crossfades over 50 ms") {
    auto loud = makePlugin("gain", 1);  // unity
    auto quiet = makePlugin("gain", 1);
    quiet->setParameter(0, 0.0f);  // silence
    auto running = stageWith({loud}, 1);
    auto fresh = stageWith({quiet}, 1);
    running->claim();

    const std::vector<float> dc(4800, 1.0f);
    run(*running, {dc});
    running->retargetTo(*fresh);
    CHECK(running->crossfading());
    const auto out = run(*running, {dc});
    const int fade = static_cast<int>(InsertStage::kCrossfadeMs * 0.001 * kSr);  // 2400
    CHECK(out[0][0] == Approx(1.0f));
    CHECK(out[0][static_cast<std::size_t>(fade / 2)] == Approx(0.5f).margin(0.01));
    CHECK(out[0][static_cast<std::size_t>(fade + 10)] == Approx(0.0f).margin(1e-6));
    CHECK(maxStep(out[0]) < 2.0f / static_cast<float>(fade));
    CHECK_FALSE(running->crossfading());
    // What was given up went to `fresh` (freed with the old graph, not here).
    CHECK_FALSE(fresh->hasChain());
}

TEST_CASE("InsertStage: processing with plugins does not allocate") {
    allocguard::setMode(allocguard::Mode::Count);
    auto stage = stageWith({makePlugin("delay", 2, 32), makePlugin("gain", 2), nullptr}, 2);
    stage->configurePad(8, InsertChain::kMinPadCapacity);
    auto fresh = stageWith({makePlugin("gain", 2)}, 2);
    fresh->configurePad(40, InsertChain::kMinPadCapacity);
    std::vector<float> l(kBlock, 0.1f), r(kBlock, -0.1f);
    float* ch[2] = {l.data(), r.data()};
    allocguard::resetViolations();
    {
        allocguard::ScopedAudioThread audio;
        for (int i = 0; i < 50; ++i) {
            if (i == 10) stage->chain().setBypassed(1, true);
            if (i == 20) stage->retargetTo(*fresh);
            stage->process(ch, 2, kBlock);
        }
    }
    CHECK(allocguard::violations() == 0);
}
