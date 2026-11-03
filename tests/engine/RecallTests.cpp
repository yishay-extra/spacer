#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <memory>
#include <vector>

#include "EngineTestUtils.h"
#include "engine/AllocationGuard.h"
#include "engine/BufferInput.h"
#include "engine/DiskStreamer.h"
#include "engine/EngineProcessor.h"
#include "engine/OfflineRenderer.h"
#include "engine/RecallPlanner.h"
#include "engine/SourceInputFactory.h"

using Catch::Approx;
using namespace spacer;
using namespace spacer::testing;

namespace {

constexpr double kSr = 48000.0;
constexpr int kBlock = 128;

GraphBuilder::InputFactory dcInputs() {
    return [](const SourceSpec&) -> std::unique_ptr<SourceInput> {
        juce::AudioBuffer<float> b(1, 48000 * 8);
        for (int i = 0; i < b.getNumSamples(); ++i) b.setSample(0, i, 0.5f);
        return std::make_unique<BufferInput>(std::move(b));
    };
}

SourceSpec directSource(int id, int speakerId) {
    SourceSpec s = makeSource(id, {}, SpatializerKind::Direct);
    s.directSpeakers = {speakerId};
    return s;
}

/// Two speakers on channels 0 / 1 (±90°, 2 m); each source is Direct to one.
Project twoChannelProject(std::vector<SourceSpec> sources) {
    Project p =
        makeProject({makeSpeaker(1, -90.0f, 2.0f, 0), makeSpeaker(2, 90.0f, 2.0f, 1)}, std::move(sources));
    return p;
}

/// Engine + live project + a recorder of everything it outputs.
struct Rig {
    EngineProcessor proc;
    Project project;
    std::vector<std::vector<float>> rec{2};

    explicit Rig(Project p) : project(std::move(p)) {
        GraphBuilder b(project);
        b.setInputFactory(dcInputs());
        proc.setGraphWhileStopped(b.build(kSr, kBlock));
    }

    void run(int samples) {
        juce::AudioBuffer<float> out(2, kBlock);
        for (int done = 0; done < samples; done += kBlock) {
            float* ptrs[] = {out.getWritePointer(0), out.getWritePointer(1)};
            proc.process(nullptr, 0, ptrs, 2, kBlock);
            for (int c = 0; c < 2; ++c)
                rec[static_cast<std::size_t>(c)].insert(rec[static_cast<std::size_t>(c)].end(), ptrs[c],
                                                        ptrs[c] + kBlock);
            Feedback f;
            while (proc.popFeedback(f)) {
                if (const auto* fired = std::get_if<fb::CueFired>(&f)) firedTokens.push_back(fired->token);
            }
            proc.collectGarbage();
        }
    }
    int pos() const { return static_cast<int>(rec[0].size()); }
    float at(int ch, int i) const { return rec[static_cast<std::size_t>(ch)][static_cast<std::size_t>(i)]; }

    /// Pushes a plan for `target` and, like the app does, applies it to the live model.
    bool recall(const Scene& target, float seconds, bool restart = false, double atBeat = -1.0,
                int token = 0) {
        RecallRequest r;
        r.project = &project;
        r.target = &target;
        r.transitionSeconds = seconds;
        r.restartMotionPhase = restart;
        r.sampleRate = kSr;
        r.maxBlockSize = kBlock;
        r.inputFactory = dcInputs();
        r.atBeat = atBeat;
        r.token = token;
        auto plan = buildRecallPlan(r);
        EngineProcessor* e = &proc;
        const bool ok = e->push(cmd::RecallScene{plan.release()});
        applyScene(project, target);
        return ok;
    }

    std::vector<int> firedTokens;
};

Scene sceneOf(const Project& p, const char* name = "target") {
    return captureScene(p, name);
}

}  // namespace

TEST_CASE("Recall: a gain change ramps linearly and completes in the stated time") {
    Rig rig(twoChannelProject({directSource(1, 1)}));
    rig.run(4800);  // settled at 0.5
    const int L = rig.proc.latencySamples();
    REQUIRE(rig.at(0, rig.pos() - 1) == Approx(0.5f));

    Scene target = sceneOf(rig.project);
    target.sources[0].gainDb = -20.0f;  // linear 1.0 -> 0.1
    const int t0 = rig.pos();
    rig.recall(target, 1.0f);
    rig.run(48000 + 4800);

    const float end = 0.5f * 0.1f;
    // Before the block that carries the command nothing has moved; the ramp
    // starts at t0 (+ limiter latency at the output) ...
    CHECK(rig.at(0, t0 + L - 1) == Approx(0.5f));
    // ... is linear in the linear domain: halfway through the gain is 0.55.
    CHECK(rig.at(0, t0 + L + 24000) == Approx(0.5f * 0.55f).margin(1e-3));
    CHECK(rig.at(0, t0 + L + 12000) == Approx(0.5f * 0.775f).margin(1e-3));
    // ... and has reached the target after exactly one second (not before).
    CHECK(rig.at(0, t0 + L + 47000) > end + 0.0005f);
    CHECK(rig.at(0, t0 + L + 48000 + 2) == Approx(end).margin(1e-5));
    CHECK(rig.at(0, rig.pos() - 1) == Approx(end).margin(1e-6));
    // The other speaker is untouched.
    CHECK(rig.at(1, rig.pos() - 1) == 0.0f);
    // The live project took the scene.
    CHECK(rig.project.sources[0].gainDb == -20.0f);
}

TEST_CASE("Recall: master gain, speaker trim and mute ramp over the transition") {
    Rig rig(twoChannelProject({directSource(1, 1), directSource(2, 2)}));
    rig.run(4800);
    const int L = rig.proc.latencySamples();

    Scene target = sceneOf(rig.project);
    target.master.gainDb = -6.0f;
    SpeakerChainSpec c;
    c.speakerId = 2;
    c.trimDb = -12.0f;
    target.speakerChains.push_back(c);
    target.sources[0].mute = true;
    const int t0 = rig.pos();
    rig.recall(target, 0.5f);
    rig.run(24000 + 4800);

    // Source 1 is muted: channel 0 ramps 0.5 -> 0 over 0.5 s, while the master
    // ramps 1 -> 0.501 (both linear): halfway 0.25 * 0.75.
    CHECK(rig.at(0, t0 + L + 12000) == Approx(0.25f * 0.75f).margin(3e-3));
    CHECK(rig.at(0, rig.pos() - 1) == 0.0f);
    // Channel 1: trim -12 dB and master -6 dB, both settled.
    const float expected =
        0.5f * juce::Decibels::decibelsToGain(-12.0f) * juce::Decibels::decibelsToGain(-6.0f);
    CHECK(rig.at(1, rig.pos() - 1) == Approx(expected).margin(1e-5));
}

TEST_CASE("Recall: a manual position moves over the transition") {
    Project p = twoChannelProject({makeSource(1, {-45.0f, 0.0f, 2.0f})});
    Rig rig(std::move(p));
    rig.run(4800);
    const int L = rig.proc.latencySamples();
    // Source at -45 deg between speakers at -90 / +90: left louder.
    REQUIRE(rig.at(0, rig.pos() - 1) > rig.at(1, rig.pos() - 1));

    Scene target = sceneOf(rig.project);
    std::get<ManualMotion>(target.sources[0].motion.value).position = {45.0f, 0.0f, 2.0f};
    const int t0 = rig.pos();
    rig.recall(target, 0.5f);
    rig.run(24000 + 4800);

    // Halfway the source is straight ahead: equal gains.
    CHECK(rig.at(0, t0 + L + 12000) == Approx(rig.at(1, t0 + L + 12000)).margin(0.02));
    // At the end it is mirrored.
    const int e = rig.pos() - 1;
    CHECK(rig.at(1, e) > rig.at(0, e));
    CHECK(rig.at(1, e) == Approx(rig.at(0, t0 + L - 1)).margin(1e-3));
}

TEST_CASE("Recall: spread, width and distance parameters ramp (live values)") {
    Rig rig(twoChannelProject({makeSource(1, {0.0f, 0.0f, 2.0f})}));
    rig.run(1000);
    Scene target = sceneOf(rig.project);
    target.sources[0].spread = 0.8f;
    target.sources[0].distance.k = 1.0f;
    rig.recall(target, 0.2f);
    rig.run(9600 + 1000);
    const SourceNode& s = rig.proc.currentGraph()->source(0);
    CHECK(s.spread() == Approx(0.8f));
    CHECK(s.distanceModel().k == Approx(1.0f));
}

TEST_CASE("Recall: a discrete change crossfades over 50 ms with no dip") {
    // Source 1 is Direct to speaker 1; the target sends it to speaker 2.
    Rig rig(twoChannelProject({directSource(1, 1)}));
    rig.run(4800);
    const int L = rig.proc.latencySamples();

    Scene target = sceneOf(rig.project);
    target.sources[0].directSpeakers = {2};
    const int t0 = rig.pos();
    rig.recall(target, 3.0f);  // a long transition must not slow the discrete switch
    rig.run(9600);

    const int fade = 2400;  // 50 ms
    CHECK(rig.at(0, t0 + L - 1) == Approx(0.5f));
    CHECK(rig.at(1, t0 + L - 1) == 0.0f);
    for (int i = 0; i < fade + 200; ++i) {
        const float sum = rig.at(0, t0 + L + i) + rig.at(1, t0 + L + i);
        CHECK(sum == Approx(0.5f).margin(1e-3));  // constant total: linear crossfade
    }
    CHECK(rig.at(0, t0 + L + fade / 2) == Approx(0.25f).margin(1e-3));
    CHECK(rig.at(0, t0 + L + fade + 10) == 0.0f);
    CHECK(rig.at(1, t0 + L + fade + 10) == Approx(0.5f).margin(1e-6));
    CHECK(rig.at(1, rig.pos() - 1) == Approx(0.5f));
}

TEST_CASE("Recall: spatializer kind switch crossfades") {
    Rig rig(twoChannelProject({makeSource(1, {-90.0f, 0.0f, 2.0f})}));  // VBAP hard left
    rig.run(4800);
    REQUIRE(rig.at(0, rig.pos() - 1) == Approx(0.5f).margin(1e-3));
    REQUIRE(rig.at(1, rig.pos() - 1) == Approx(0.0f).margin(1e-3));

    Scene target = sceneOf(rig.project);
    target.sources[0].spatializer = SpatializerKind::Dbap;
    rig.recall(target, 1.0f);
    rig.run(9600);
    // Now DBAP: some energy on the far speaker as well, and no gap on the way.
    CHECK(rig.at(1, rig.pos() - 1) > 0.01f);
    float minLeft = 1.0f;
    for (int i = rig.pos() - 9600; i < rig.pos(); ++i) minLeft = std::min(minLeft, rig.at(0, i));
    CHECK(minLeft > 0.3f);
}

TEST_CASE("Recall: added sources fade in, removed sources fade out and then stay silent") {
    SourceSpec one = directSource(1, 1);
    one.gainDb = -12.0f;  // keeps the sum on speaker 1 below the limiter ceiling
    Rig rig(twoChannelProject({one, directSource(2, 2)}));
    rig.run(4800);
    const int L = rig.proc.latencySamples();
    const float g12 = juce::Decibels::decibelsToGain(-12.0f);

    Scene target = sceneOf(rig.project);
    target.sources.erase(target.sources.begin() + 1);  // remove source 2 (speaker 2)
    SourceSpec three = directSource(3, 1);
    three.gainDb = -12.0f;
    target.sources.push_back(three);  // add source 3 (also speaker 1)
    const int t0 = rig.pos();
    rig.recall(target, 1.0f);
    rig.run(48000 + 4800);

    // Source 2 fades 0.5 -> 0 over the transition (1 s), source 3 fades 0 -> 0.5 on speaker 1.
    CHECK(rig.at(1, t0 + L + 24000) == Approx(0.25f).margin(2e-3));
    CHECK(rig.at(1, rig.pos() - 1) == 0.0f);
    CHECK(rig.at(0, t0 + L + 24000) == Approx(0.5f * g12 * 1.5f).margin(2e-3));
    CHECK(rig.at(0, rig.pos() - 1) == Approx(2.0f * 0.5f * g12).margin(1e-4));
    CHECK(rig.proc.currentGraph()->numSources() == 2);
    CHECK(rig.proc.currentGraph()->findSource(2) == nullptr);
}

TEST_CASE("Recall: a ramp keeps going across a second recall; the newest wins per parameter") {
    Rig rig(twoChannelProject({directSource(1, 1)}));
    rig.run(4800);
    Scene a = sceneOf(rig.project);
    a.sources[0].gainDb = -20.0f;
    rig.recall(a, 1.0f);
    rig.run(12000);  // a quarter of the way
    Scene b = a;
    b.sources[0].gainDb = 0.0f;
    rig.recall(b, 0.5f);  // back up, from wherever it is
    rig.run(24000 + 4800);
    CHECK(rig.at(0, rig.pos() - 1) == Approx(0.5f).margin(1e-5));
}

TEST_CASE("Recall: scheduled recall fires at the exact sample the transport reaches the beat") {
    // 120 bpm: 24000 samples per beat. Beat 2.0005 = sample 48012.
    Rig rig(twoChannelProject({directSource(1, 1)}));
    rig.proc.push(cmd::ClockContinue{});
    rig.run(4800);
    const int L = rig.proc.latencySamples();

    Scene target = sceneOf(rig.project);
    target.sources[0].gainDb = -40.0f;
    REQUIRE(rig.recall(target, 0.0f, false, 2.0005, 7));
    rig.run(48000);

    // Nothing moved before the beat; the ramp starts at sample 48012 exactly.
    int firstChange = -1;
    for (int i = 4800; i < rig.pos(); ++i)  // after the initial fade-in
        if (std::abs(rig.at(0, i) - 0.5f) > 1e-7f) {
            firstChange = i;
            break;
        }
    CHECK(firstChange == 48012 + L);
    REQUIRE(rig.firedTokens.size() == 1);
    CHECK(rig.firedTokens[0] == 7);
}

TEST_CASE("Recall: a quantised transport start triggers a beat-0 recall at the start sample") {
    // Transport armed at beat 3.5, bar boundary (beat 4) is 12000 samples away.
    Project p = twoChannelProject({directSource(1, 1)});
    p.clock.quantisedStart = true;
    EngineProcessor proc;
    proc.configureClock(p.clock);
    GraphBuilder b(p);
    b.setInputFactory(dcInputs());
    proc.setGraphWhileStopped(b.build(kSr, kBlock));
    proc.push(cmd::ClockSetBeat{3.5});
    proc.push(cmd::ClockPlay{});

    Scene target = captureScene(p, "t");
    target.sources[0].gainDb = -40.0f;
    RecallRequest r;
    r.project = &p;
    r.target = &target;
    r.sampleRate = kSr;
    r.maxBlockSize = kBlock;
    r.inputFactory = dcInputs();
    r.atBeat = 4.0;
    r.token = 1;
    proc.push(cmd::RecallScene{buildRecallPlan(r).release()});

    std::vector<float> out;
    juce::AudioBuffer<float> buf(2, kBlock);
    for (int i = 0; i < 200; ++i) {
        float* ptrs[] = {buf.getWritePointer(0), buf.getWritePointer(1)};
        proc.process(nullptr, 0, ptrs, 2, kBlock);
        out.insert(out.end(), ptrs[0], ptrs[0] + kBlock);
    }
    int first = -1;
    for (int i = 4800; i < static_cast<int>(out.size()); ++i)
        if (std::abs(out[static_cast<std::size_t>(i)] - 0.5f) > 1e-7f) {
            first = i;
            break;
        }
    // Start falls at sample 12000 (inside block 93, offset 96), not at a block edge.
    CHECK(first == 12000 + proc.latencySamples());
}

TEST_CASE("Recall: scheduled recall does not fire while stopped, can be cancelled and replaced") {
    Rig rig(twoChannelProject({directSource(1, 1)}));
    Scene quiet = sceneOf(rig.project);
    quiet.sources[0].gainDb = -40.0f;

    rig.recall(quiet, 0.0f, false, 1.0, 1);
    rig.run(9600);  // transport stopped
    CHECK(rig.firedTokens.empty());
    CHECK(rig.at(0, rig.pos() - 1) == Approx(0.5f));

    rig.proc.push(cmd::CancelScheduledRecall{});
    rig.proc.push(cmd::ClockContinue{});
    rig.run(48000);
    CHECK(rig.firedTokens.empty());
    CHECK(rig.at(0, rig.pos() - 1) == Approx(0.5f));

    // Two schedules: only the second fires.
    rig.recall(quiet, 0.0f, false, 100.0, 2);
    rig.recall(quiet, 0.0f, false, 4.0, 3);
    rig.run(96000);
    REQUIRE(rig.firedTokens.size() == 1);
    CHECK(rig.firedTokens[0] == 3);
}

TEST_CASE("Recall: an immediate recall reports its token") {
    Rig rig(twoChannelProject({directSource(1, 1)}));
    Scene t = sceneOf(rig.project);
    t.master.gainDb = -3.0f;
    rig.recall(t, 0.1f, false, -1.0, 42);
    rig.run(kBlock);
    REQUIRE(rig.firedTokens.size() == 1);
    CHECK(rig.firedTokens[0] == 42);
}

TEST_CASE("Recall: neither flat nor structural recalls allocate on the audio thread") {
    Rig rig(twoChannelProject({directSource(1, 1), directSource(2, 2)}));
    rig.proc.push(cmd::ClockContinue{});
    rig.run(4800);

    // Prepare plans first (non-RT), then push them under the guard.
    Scene flat = sceneOf(rig.project);
    flat.sources[0].gainDb = -6.0f;
    flat.sources[1].spread = 0.5f;
    flat.master.gainDb = -2.0f;
    Scene structural = flat;
    structural.sources[0].directSpeakers = {2};
    structural.sources.erase(structural.sources.begin() + 1);
    structural.sources.push_back(directSource(3, 1));

    allocguard::setMode(allocguard::Mode::Count);
    allocguard::resetViolations();
    rig.recall(flat, 0.2f);
    rig.run(9600);
    rig.recall(structural, 0.2f, true);
    rig.run(9600);
    rig.recall(sceneOf(rig.project), 0.0f, false, 12.0, 5);
    rig.run(48000);
    // Plan building allocates (message thread) but runs outside the audio scope;
    // collectGarbage() frees in run() outside it as well.
    CHECK(allocguard::violations() == 0);
}

TEST_CASE("Recall: a replaced file source truly crossfades (the ghost mirrors the stream, WP14)") {
    const auto dir = juce::File::createTempFile("spacer-file-recall");
    REQUIRE(dir.createDirectory());
    // A slow ramp: any jump in file position shows up as a step.
    const auto wav = dir.getChildFile("ramp.wav");
    const int length = 48000 * 4;
    {
        OfflineRenderer::Result ramp;
        ramp.sampleRate = kSr;
        ramp.audio.setSize(1, length);
        for (int i = 0; i < length; ++i)
            ramp.audio.setSample(0, i, 0.2f + 0.6f * static_cast<float>(i) / length);
        REQUIRE_FALSE(OfflineRenderer::writeWav(ramp, wav).has_value());
    }
    SourceSpec s = directSource(1, 1);
    FileInputSpec f;
    f.path = wav.getFullPathName().toStdString();
    s.input.value = f;
    Project project = twoChannelProject({s});

    DiskStreamer streamer;
    auto factory = std::make_shared<SourceInputFactory>();
    factory->setDiskStreamer(&streamer);
    const GraphBuilder::InputFactory inputs = [factory](const SourceSpec& spec) {
        return factory->create(spec);
    };
    EngineProcessor proc;
    {
        GraphBuilder b(project);
        b.setInputFactory(inputs);
        proc.setGraphWhileStopped(b.build(kSr, kBlock));
    }
    std::vector<std::vector<float>> rec{2};
    auto run = [&](int samples) {
        juce::AudioBuffer<float> out(2, kBlock);
        for (int done = 0; done < samples; done += kBlock) {
            streamer.serviceAll();
            float* ptrs[] = {out.getWritePointer(0), out.getWritePointer(1)};
            proc.process(nullptr, 0, ptrs, 2, kBlock);
            for (int c = 0; c < 2; ++c)
                rec[static_cast<std::size_t>(c)].insert(rec[static_cast<std::size_t>(c)].end(), ptrs[c],
                                                        ptrs[c] + kBlock);
            Feedback fb;
            while (proc.popFeedback(fb)) {
            }
            proc.collectGarbage();
        }
    };
    proc.push(cmd::FilePlay{1});
    run(48000);
    REQUIRE(rec[0].back() > 0.3f);

    Scene target = sceneOf(project);
    target.sources[0].directSpeakers = {2};
    RecallRequest r;
    r.project = &project;
    r.target = &target;
    r.transitionSeconds = 0.0f;
    r.sampleRate = kSr;
    r.maxBlockSize = kBlock;
    r.inputFactory = inputs;
    REQUIRE(proc.push(cmd::RecallScene{buildRecallPlan(r).release()}));
    const int t0 = static_cast<int>(rec[0].size());
    const int L = proc.latencySamples();
    run(9600);
    const int fade = 2400;  // 50 ms
    float maxStep = 0.0f;
    for (int i = -50; i < fade + 200; ++i) {
        const auto k = static_cast<std::size_t>(t0 + L + i);
        maxStep = std::max(maxStep, std::abs(rec[0][k] + rec[1][k] - rec[0][k - 1] - rec[1][k - 1]));
    }
    CHECK(maxStep < 1e-4f);  // the sum continues the ramp: no gap, no restart
    const auto mid = static_cast<std::size_t>(t0 + L + fade / 2);
    CHECK(rec[0][mid] == Approx(rec[1][mid]).margin(1e-3));  // half way through the crossfade
    dir.deleteRecursively();
}
