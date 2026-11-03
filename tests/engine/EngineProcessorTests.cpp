#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <string>
#include <vector>

#include "EngineTestUtils.h"
#include "TestReaders.h"
#include "engine/AllocationGuard.h"
#include "engine/BufferInput.h"
#include "engine/DiskStreamer.h"
#include "engine/EngineProcessor.h"
#include "engine/FilePlayer.h"
#include "engine/GraphBuilder.h"

using Catch::Approx;
using namespace spacer;
using namespace spacer::testing;

namespace {

constexpr double kSr = 48000.0;
constexpr int kBlock = 128;

/// A DC input (constant 0.5) for every source, so levels are easy to read.
GraphBuilder::InputFactory dcInputs() {
    return [](const SourceSpec&) -> std::unique_ptr<SourceInput> {
        juce::AudioBuffer<float> b(1, 48000 * 4);
        for (int i = 0; i < b.getNumSamples(); ++i) b.setSample(0, i, 0.5f);
        return std::make_unique<BufferInput>(std::move(b));
    };
}

/// Two speakers on channels 0/1 at the same distance; sources are Direct to one speaker each.
Project twoChannelProject(std::vector<int> sourceIds) {
    Project p = makeProject({makeSpeaker(1, -90.0f, 2.0f, 0), makeSpeaker(2, 90.0f, 2.0f, 1)});
    for (int id : sourceIds) {
        SourceSpec s = makeSource(id, {}, SpatializerKind::Direct);
        s.directSpeakers = {id == 1 ? 1 : 2};
        p.sources.push_back(s);
    }
    return p;
}

struct Rig {
    EngineProcessor proc;
    juce::AudioBuffer<float> out{2, 4096};

    explicit Rig(const Project& p) {
        GraphBuilder b(p);
        b.setInputFactory(dcInputs());
        proc.setGraphWhileStopped(b.build(kSr, kBlock));
    }
    /// One block; returns the output buffer.
    const juce::AudioBuffer<float>& run(int n = kBlock) {
        float* ptrs[] = {out.getWritePointer(0), out.getWritePointer(1)};
        proc.process(nullptr, 0, ptrs, 2, n);
        return out;
    }
    void runFor(int blocks) {
        for (int i = 0; i < blocks; ++i) run();
    }
    float last(int ch, int n = kBlock) const { return out.getSample(ch, n - 1); }
};

/// Graphs built on demand, driven like the offline renderer (streamer
/// serviced before each block). Sources in `fileIds` play a RampReader file
/// (stopped until FilePlay, same identity in every graph); the rest are DC.
struct SwapRig {
    DiskStreamer streamer;
    EngineProcessor proc;
    juce::AudioBuffer<float> out{2, kBlock};
    fb::FileTransport lastTransport{};

    std::unique_ptr<Graph> build(std::vector<int> ids, std::vector<int> fileIds = {}, double sr = kSr) {
        GraphBuilder b(twoChannelProject(std::move(ids)));
        auto dc = dcInputs();
        b.setInputFactory([this, dc, fileIds](const SourceSpec& s) -> std::unique_ptr<SourceInput> {
            if (std::find(fileIds.begin(), fileIds.end(), s.id) == fileIds.end()) return dc(s);
            return std::make_unique<FilePlayer>(FileInputSpec{}, 1, std::make_unique<RampReader>(kSr, 480000),
                                                &streamer, "file" + std::to_string(s.id));
        });
        return b.build(sr, kBlock);
    }
    void run(int blocks = 1) {
        for (int i = 0; i < blocks; ++i) {
            streamer.serviceAll();
            float* ptrs[] = {out.getWritePointer(0), out.getWritePointer(1)};
            proc.process(nullptr, 0, ptrs, 2, kBlock);
            Feedback f;
            while (proc.popFeedback(f))
                if (auto* t = std::get_if<fb::FileTransport>(&f)) lastTransport = *t;
        }
    }
    bool filePlaying(int id) {
        TransportState t;
        SourceNode* s = proc.currentGraph()->findSource(id);
        return s != nullptr && s->input() != nullptr && s->input()->transportState(t) && t.playing;
    }
};

}  // namespace

TEST_CASE("EngineProcessor: new sources fade in over 10 ms") {
    Rig rig(twoChannelProject({1}));
    rig.runFor(10);  // 1280 samples > 480 + latency
    CHECK(rig.last(0) == Approx(0.5f));
    CHECK(rig.last(1) == 0.0f);
}

TEST_CASE("EngineProcessor: SetSourceGain ramps to the new gain") {
    Rig rig(twoChannelProject({1}));
    rig.runFor(10);
    REQUIRE(rig.proc.push(cmd::SetSourceGain{1, -6.0f, 20.0f}));
    rig.runFor(20);
    CHECK(rig.last(0) == Approx(0.5f * juce::Decibels::decibelsToGain(-6.0f)).margin(1e-4));
}

TEST_CASE("EngineProcessor: master gain and panic act on every output") {
    Rig rig(twoChannelProject({1, 2}));
    rig.runFor(10);
    rig.proc.push(cmd::SetMasterGain{-6.0f, 0.0f});
    rig.run();
    CHECK(rig.last(0) == Approx(0.5f * juce::Decibels::decibelsToGain(-6.0f)).margin(1e-4));
    CHECK(rig.last(1) == Approx(0.5f * juce::Decibels::decibelsToGain(-6.0f)).margin(1e-4));
    rig.proc.push(cmd::SetPanic{true});
    rig.runFor(5);
    CHECK(rig.last(0) == 0.0f);
    CHECK(rig.last(1) == 0.0f);
    rig.proc.push(cmd::SetPanic{false});
    rig.runFor(5);
    CHECK(rig.last(0) > 0.2f);
}

TEST_CASE("EngineProcessor: speaker commands reach the speaker chain") {
    Rig rig(twoChannelProject({1, 2}));
    rig.runFor(10);
    rig.proc.push(cmd::SetSpeakerMute{2, true, 1.0f});
    rig.proc.push(cmd::SetSpeakerTrim{1, -20.0f, 1.0f});
    rig.runFor(3);
    CHECK(rig.last(0) == Approx(0.05f).margin(1e-4));
    CHECK(rig.last(1) == 0.0f);
}

TEST_CASE("EngineProcessor: blocks longer than the graph's max block are split") {
    Rig rig(twoChannelProject({1}));
    rig.run(1000);
    rig.run(1000);
    CHECK(rig.out.getSample(0, 999) == Approx(0.5f));
}

TEST_CASE("EngineProcessor: SwapGraph carries state, fades out removed and fades in added sources") {
    Rig rig(twoChannelProject({1, 2}));
    rig.runFor(10);
    REQUIRE(rig.last(0) == Approx(0.5f));
    REQUIRE(rig.last(1) == Approx(0.5f));

    // New graph: source 1 kept (gain changed to -6 dB), source 2 removed, source 3 added (→ speaker 2).
    Project next = twoChannelProject({1});
    next.sources[0].gainDb = -6.0f;
    SourceSpec added = makeSource(3, {}, SpatializerKind::Direct);
    added.directSpeakers = {2};
    next.sources.push_back(added);
    GraphBuilder b(next);
    b.setInputFactory(dcInputs());
    REQUIRE(rig.proc.swapGraph(b.build(kSr, kBlock)));

    allocguard::setMode(allocguard::Mode::Count);
    allocguard::resetViolations();

    // Block 1: old graph, source 2 fades out over exactly this block.
    rig.run();
    const int L = rig.proc.latencySamples();
    // Source 2's fade started at the gain stage; after the limiter delay the
    // output is still the old level at the block start and falling at the end.
    CHECK(rig.out.getSample(1, L) < 0.5f);
    CHECK(rig.out.getSample(1, L) > 0.45f);
    // Source 1 is untouched in the old graph.
    CHECK(rig.last(0) == Approx(0.5f));

    // Next blocks: new graph. Source 1 continues from 0.5 and ramps to -6 dB
    // (no restart from 0, no gap); source 3 fades in on speaker 2.
    rig.run();
    CHECK(rig.out.getSample(0, 0) == Approx(0.5f).margin(0.01));
    rig.runFor(10);
    CHECK(rig.last(0) == Approx(0.5f * juce::Decibels::decibelsToGain(-6.0f)).margin(1e-4));
    CHECK(rig.last(1) == Approx(0.5f));

    CHECK(allocguard::violations() == 0);
    // The old graph comes back for deletion on the control thread.
    CHECK(rig.proc.collectGarbage() == 1);
}

TEST_CASE("EngineProcessor: SwapGraph keeps speaker chains and crossfades a changed delay") {
    Rig rig(twoChannelProject({1}));
    rig.runFor(10);
    Project next = twoChannelProject({1});
    next.layout.speakers[0].userDelayMs = 5.0f;
    GraphBuilder b(next);
    b.setInputFactory(dcInputs());
    rig.proc.swapGraph(b.build(kSr, kBlock));
    // DC through a crossfade between two delays of the same DC stays DC: a
    // carried chain shows no dip; a fresh chain would output silence for 5 ms.
    float minOut = 1.0f;
    for (int i = 0; i < 40; ++i) {
        rig.run();
        minOut = std::min(minOut,
                          *std::min_element(rig.out.getReadPointer(0), rig.out.getReadPointer(0) + kBlock));
    }
    CHECK(minOut == Approx(0.5f).margin(1e-4));
    CHECK(rig.proc.currentGraph()->speaker(0).targetDelaySamples() == Approx(240.0f));
}

TEST_CASE("EngineProcessor: steady-state processing does not allocate") {
    Project p = makeProject({makeSpeaker(1, -45.0f, 2.0f, 0), makeSpeaker(2, 45.0f, 2.5f, 1)});
    for (int i = 1; i <= 8; ++i) {
        SourceSpec s = makeSource(i, {static_cast<float>(i * 40 - 180), 0.0f, 2.0f});
        s.stereo = (i % 2) == 0;
        s.spread = 0.3f;
        s.distance.k = 1.0f;
        s.distance.q = 0.5f;
        p.sources.push_back(s);
    }
    p.speakerChains = {SpeakerChainSpec{
        1, 0.0f, 0.0f, false, false, Eq3{100.0f, 3.0f, 1000.0f, -2.0f, 1.0f, 8000.0f, 1.0f}, {}}};
    Rig rig(p);
    rig.runFor(4);
    allocguard::setMode(allocguard::Mode::Count);
    allocguard::resetViolations();
    for (int i = 0; i < 200; ++i) {
        rig.proc.push(cmd::SetSourcePosition{1 + i % 8, -1, {static_cast<float>(i), 10.0f, 3.0f}, 5.0f});
        rig.proc.push(
            cmd::SetSpeakerEq{1, Eq3{120.0f, static_cast<float>(i % 7), 900.0f, 1.0f, 2.0f, 9000.0f, -1.0f}});
        rig.proc.push(cmd::SetSpeakerDelay{2, static_cast<float>(i % 3)});
        rig.proc.push(cmd::ClockSetBpm{100.0 + i});
        rig.run();
        Feedback f;
        while (rig.proc.popFeedback(f)) {
        }
    }
    CHECK(allocguard::violations() == 0);
}

TEST_CASE("EngineProcessor: publishes meters, positions and clock as feedback") {
    Project p = twoChannelProject({1});
    p.sources[0].spatializer = SpatializerKind::Vbap;
    p.sources[0].motion.value = ManualMotion{{-90.0f, 0.0f, 2.0f}, 0.0f};
    Rig rig(p);
    rig.proc.push(cmd::ClockContinue{});
    rig.runFor(20);
    bool sawSource = false, sawSpeaker = false, sawPosition = false, sawClock = false;
    Feedback f;
    while (rig.proc.popFeedback(f)) {
        if (auto* m = std::get_if<fb::SourceMeter>(&f))
            sawSource = sawSource || (m->sourceId == 1 && m->peak[0] > 0.4f);
        if (auto* m = std::get_if<fb::SpeakerMeter>(&f))
            sawSpeaker = sawSpeaker || (m->speakerId == 1 && m->peak > 0.4f);
        if (auto* m = std::get_if<fb::SourcePosition>(&f))
            sawPosition = sawPosition || (m->sourceId == 1 && std::abs(m->position.azimuth + 90.0f) < 0.01f);
        if (auto* m = std::get_if<fb::Clock>(&f)) sawClock = sawClock || m->state.playing;
    }
    CHECK(sawSource);
    CHECK(sawSpeaker);
    CHECK(sawPosition);
    CHECK(sawClock);
}

TEST_CASE("EngineProcessor: manual position commands move the source with smoothing") {
    Project p = makeProject({makeSpeaker(1, -90.0f, 2.0f, 0), makeSpeaker(2, 90.0f, 2.0f, 1)},
                            {makeSource(1, {-90.0f, 0.0f, 2.0f})});
    Rig rig(p);
    rig.runFor(10);
    CHECK(rig.last(1) == 0.0f);
    rig.proc.push(cmd::SetSourcePosition{1, -1, {90.0f, 0.0f, 2.0f}, 20.0f});
    rig.run();
    // Smoothing: one block later the source has not jumped all the way.
    CHECK(rig.last(0) > 0.05f);
    rig.runFor(100);  // > 250 ms = 12 time constants
    CHECK(rig.last(0) < 1e-3f);
    CHECK(rig.last(1) == Approx(0.5f).margin(1e-3));
}

TEST_CASE("EngineProcessor: stereo sources render at centre ± width/2") {
    Project p = makeProject({makeSpeaker(1, -90.0f, 2.0f, 0), makeSpeaker(2, 90.0f, 2.0f, 1)});
    SourceSpec s = makeSource(1, {0.0f, 0.0f, 2.0f});
    s.stereo = true;
    s.width = 180.0f;  // L at -90, R at +90
    p.sources = {s};
    EngineProcessor proc;
    GraphBuilder b(p);
    b.setInputFactory([](const SourceSpec&) -> std::unique_ptr<SourceInput> {
        juce::AudioBuffer<float> buf(2, 48000);
        for (int i = 0; i < buf.getNumSamples(); ++i) {
            buf.setSample(0, i, 0.25f);
            buf.setSample(1, i, -0.5f);
        }
        return std::make_unique<BufferInput>(std::move(buf));
    });
    proc.setGraphWhileStopped(b.build(kSr, kBlock));
    juce::AudioBuffer<float> out(2, kBlock);
    float* ptrs[] = {out.getWritePointer(0), out.getWritePointer(1)};
    for (int i = 0; i < 10; ++i) proc.process(nullptr, 0, ptrs, 2, kBlock);
    CHECK(out.getSample(0, kBlock - 1) == Approx(0.25f).margin(1e-4));
    CHECK(out.getSample(1, kBlock - 1) == Approx(-0.5f).margin(1e-4));
}

TEST_CASE("EngineProcessor: SwapGraph then FilePlay in one drain starts the file") {
    SwapRig rig;
    rig.proc.configureClock(ClockConfig{});
    REQUIRE(rig.proc.swapGraph(rig.build({1}, {1})));
    REQUIRE(rig.proc.push(cmd::FilePlay{1}));
    rig.run();
    CHECK(rig.lastTransport.sourceId == 1);
    CHECK(rig.lastTransport.playing);
    CHECK(rig.lastTransport.positionSeconds == Approx(kBlock / kSr));
    rig.run();
    CHECK(peakAbs(rig.out.getReadPointer(0), kBlock) > 0.0f);
    CHECK(rig.proc.droppedCommands() == 0);
}

TEST_CASE("EngineProcessor: commands for a source only the new graph has survive a fade-out swap") {
    SwapRig rig;
    rig.proc.setGraphWhileStopped(rig.build({1, 2}));
    rig.run(10);
    REQUIRE(rig.proc.swapGraph(rig.build({1, 3})));
    REQUIRE(rig.proc.push(cmd::SetSourceMute{3, true, 0.0f}));
    rig.run();  // source 2 fades out in the old graph; the swap lands after the block
    REQUIRE(rig.proc.currentGraph()->findSource(3) != nullptr);
    rig.run();
    CHECK(rig.proc.currentGraph()->findSource(3)->currentGain() == 0.0f);
    rig.run(10);
    CHECK(rig.out.getSample(1, kBlock - 1) == 0.0f);
    CHECK(rig.proc.droppedCommands() == 0);
}

TEST_CASE("EngineProcessor: a timed command due during a pending swap is applied after it") {
    SwapRig rig;
    rig.proc.setGraphWhileStopped(rig.build({1, 2}));
    rig.run(10);
    const std::int64_t block1Start = rig.proc.snapshot().blockStartSample + kBlock;
    REQUIRE(rig.proc.swapGraph(rig.build({1, 3})));
    REQUIRE(rig.proc.pushAt(cmd::SetSourceMute{3, true, 0.0f}, block1Start + kBlock / 2));
    rig.run();
    REQUIRE(rig.proc.currentGraph()->findSource(3) != nullptr);
    rig.run();
    CHECK(rig.proc.currentGraph()->findSource(3)->currentGain() == 0.0f);
    rig.run(10);
    CHECK(rig.out.getSample(1, kBlock - 1) == 0.0f);
    CHECK(rig.proc.droppedCommands() == 0);
}

TEST_CASE(
    "EngineProcessor: a SwapGraph behind a held command lands after the pending one and the held command "
    "survives") {
    SwapRig rig;
    rig.proc.setGraphWhileStopped(rig.build({1, 2}));
    rig.run(10);
    REQUIRE(rig.proc.swapGraph(rig.build({1, 3}, {3})));
    // Source gain is not carried across a swap (the new graph's spec wins), so
    // the held command is a file transport command, which is carried.
    REQUIRE(rig.proc.push(cmd::FilePlay{3}));
    auto b = rig.build({1, 3}, {3});
    const Graph* graphB = b.get();
    REQUIRE(rig.proc.swapGraph(std::move(b)));
    rig.run(3);
    CHECK(rig.proc.currentGraph() == graphB);
    CHECK(rig.filePlaying(3));
    CHECK(rig.proc.droppedCommands() == 0);
}

TEST_CASE("EngineProcessor: a newer SwapGraph supersedes a pending one in the same drain") {
    SwapRig rig;
    rig.proc.setGraphWhileStopped(rig.build({1, 2}));
    rig.run(10);
    auto a = rig.build({1});
    const Graph* graphA = a.get();
    auto b = rig.build({1, 3});
    const Graph* graphB = b.get();
    REQUIRE(rig.proc.swapGraph(std::move(a)));
    REQUIRE(rig.proc.swapGraph(std::move(b)));
    bool aWasLive = false;
    for (int i = 0; i < 4; ++i) {
        rig.run();
        aWasLive = aWasLive || rig.proc.currentGraph() == graphA;
    }
    CHECK_FALSE(aWasLive);
    CHECK(rig.proc.currentGraph() == graphB);
    CHECK(rig.proc.droppedCommands() == 0);
}

TEST_CASE("EngineProcessor: setGraphWhileStopped drains everything queued before it") {
    SwapRig rig;
    rig.proc.setGraphWhileStopped(rig.build({1, 2}));
    rig.run(10);
    REQUIRE(rig.proc.swapGraph(rig.build({1})));
    REQUIRE(rig.proc.push(cmd::SetSourceGain{1, -6.0f, 0.0f}));
    REQUIRE(rig.proc.swapGraph(rig.build({1, 3})));
    auto device = rig.build({1, 2}, {}, 44100.0);
    const Graph* deviceGraph = device.get();
    rig.proc.setGraphWhileStopped(std::move(device));
    rig.run(2);
    CHECK(rig.proc.currentGraph() == deviceGraph);
    CHECK(rig.proc.currentGraph()->sampleRate() == 44100.0);
    CHECK(rig.proc.droppedCommands() == 0);
}
