// Motion and modulators driven by the engine (WP7 wired into SourceNode).

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <memory>

#include "EngineTestUtils.h"
#include "engine/AllocationGuard.h"
#include "engine/BufferInput.h"
#include "engine/DiskStreamer.h"
#include "engine/EngineProcessor.h"
#include "engine/GraphBuilder.h"
#include "engine/OfflineRenderer.h"

using Catch::Approx;
using namespace spacer;
using namespace spacer::testing;

namespace {

constexpr double kSr = 48000.0;
constexpr int kBlock = 480;  // 10 ms

Project quad() {
    // Speakers front / right / back / left on channels 0..3.
    return makeProject({makeSpeaker(1, 0.0f, 2.0f, 0), makeSpeaker(2, 90.0f, 2.0f, 1),
                        makeSpeaker(3, 180.0f, 2.0f, 2), makeSpeaker(4, -90.0f, 2.0f, 3)});
}

OrbitMotion orbit(Period period, float radius = 2.0f) {
    OrbitMotion o;
    o.centre = {0.0f, 0.0f, 0.0f};
    o.radius = radius;
    o.period = period;
    o.direction = 1;
    return o;
}

SourceSpec orbitSource(Period period, float radius = 2.0f) {
    SourceSpec s = makeSource(1, {});
    s.motion.value = orbit(period, radius);
    GeneratorSpec g;
    g.hz = 1000.0f;
    s.input.value = g;
    s.gainDb = -12.0f;
    return s;
}

/// Engine driven block by block; sources use their live inputs.
struct Rig {
    DiskStreamer streamer;
    EngineProcessor proc;
    juce::AudioBuffer<float> out;

    explicit Rig(const Project& p, int outputs = 4) : out(outputs, kBlock) {
        proc.configureClock(p.clock);
        proc.setGraphWhileStopped(build(p));
        proc.push(cmd::ClockContinue{});
    }
    std::unique_ptr<Graph> build(const Project& p) {
        GraphBuilder b(p);
        b.setDiskStreamer(&streamer);
        return b.build(kSr, kBlock);
    }
    void run(int blocks = 1) {
        for (int i = 0; i < blocks; ++i) {
            streamer.serviceAll();
            std::vector<float*> ptrs;
            for (int c = 0; c < out.getNumChannels(); ++c) ptrs.push_back(out.getWritePointer(c));
            proc.process(nullptr, 0, ptrs.data(), out.getNumChannels(), kBlock);
            Feedback f;
            while (proc.popFeedback(f)) {
                if (auto* t = std::get_if<fb::FileTransport>(&f)) lastTransport = *t;
            }
            proc.collectGarbage();
        }
    }
    SourceNode& source(int id = 1) { return *proc.currentGraph()->findSource(id); }
    Position pos(int id = 1, int ch = 0) { return source(id).renderedPosition(ch); }

    fb::FileTransport lastTransport{};
};

float wrapDeg(double deg) {
    return Position::wrapAzimuth(static_cast<float>(std::fmod(deg, 360.0)));
}

}  // namespace

TEST_CASE("Source motion: offline render of an Orbit source moves around the speakers in order") {
    for (Period period : {Period::seconds(1.0), Period::beats(2.0) /* 120 bpm */}) {
        CAPTURE(period.value);
        Project p = quad();
        p.sources = {orbitSource(period)};
        auto r = OfflineRenderer::render(p, {}, 2.0, {kSr, kBlock, {}});
        REQUIRE(r.hasValue());
        // At t = k/4 s (+1 s, after the fade-in) the orbit is at az 0, 90,
        // 180, -90 (clockwise): that speaker carries the source alone.
        for (int k = 0; k < 4; ++k) {
            CAPTURE(k);
            const int centre = static_cast<int>((1.0 + 0.25 * k) * kSr) + r->latencySamples;
            const int half = 48;  // ±1 ms ≈ ±0.36°
            float levels[4];
            for (int ch = 0; ch < 4; ++ch)
                levels[ch] = rms(r->audio.getReadPointer(ch, centre - half), 2 * half);
            const int expected = k;  // speaker order front, right, back, left
            for (int ch = 0; ch < 4; ++ch) {
                if (ch == expected)
                    CHECK(levels[ch] > 0.15f * 0.7f);
                else
                    CHECK(levels[ch] < 0.01f);
            }
        }
        // Between two speakers (az 45 at t = 1.125 s) both share the signal equally.
        const int mid = static_cast<int>(1.125 * kSr) + r->latencySamples;
        const float front = rms(r->audio.getReadPointer(0, mid - 48), 96);
        const float right = rms(r->audio.getReadPointer(1, mid - 48), 96);
        CHECK(front == Approx(right).epsilon(0.05));
    }
}

TEST_CASE("Source motion: the orbit's position follows the formula per block") {
    Project p = quad();
    p.sources = {orbitSource(Period::seconds(1.0))};
    Rig rig(p);
    for (int n = 1; n <= 150; ++n) {
        rig.run();
        const Position pos = rig.pos();
        REQUIRE(pos.distance == Approx(2.0f).margin(1e-4));
        REQUIRE(std::abs(Position::wrapAzimuth(pos.azimuth - wrapDeg(3.6 * n))) < 0.01f);
    }
}

TEST_CASE("Source motion: beats periods freeze when the transport stops, seconds periods do not") {
    Project p = quad();
    SourceSpec beats = orbitSource(Period::beats(2.0));
    SourceSpec seconds = orbitSource(Period::seconds(1.0));
    seconds.id = 2;
    p.sources = {beats, seconds};
    Rig rig(p);
    rig.run(10);
    rig.proc.push(cmd::ClockStop{});
    rig.run(1);
    const float frozen = rig.pos(1).azimuth;
    const float moving = rig.pos(2).azimuth;
    rig.run(10);
    CHECK(rig.pos(1).azimuth == Approx(frozen).margin(1e-4));
    CHECK(std::abs(Position::wrapAzimuth(rig.pos(2).azimuth - moving - 36.0f)) < 0.01f);
}

TEST_CASE("Source motion: SetMotionParam changes a parameter without restarting the phase") {
    Project p = quad();
    p.sources = {orbitSource(Period::seconds(1.0))};
    Rig rig(p);
    rig.run(10);
    rig.proc.push(cmd::SetMotionParam{1, -1, MotionParam::Radius, 3.0f});
    rig.run(5);
    CHECK(rig.pos().distance == Approx(3.0f).margin(1e-4));
    CHECK(std::abs(Position::wrapAzimuth(rig.pos().azimuth - wrapDeg(3.6 * 15))) < 0.01f);

    SECTION("a parameter the kind does not have is ignored") {
        rig.proc.push(cmd::SetMotionParam{1, -1, MotionParam::FromAz, 50.0f});
        rig.run();
        CHECK(std::abs(Position::wrapAzimuth(rig.pos().azimuth - wrapDeg(3.6 * 16))) < 0.01f);
    }
    SECTION("phaseOffset is absolute") {
        rig.proc.push(cmd::SetMotionParam{1, -1, MotionParam::PhaseOffset, 90.0f});
        rig.run();
        CHECK(std::abs(Position::wrapAzimuth(rig.pos().azimuth - wrapDeg(3.6 * 16 + 90.0))) < 0.01f);
    }
}

TEST_CASE("Source motion: ResetMotion puts phase 0 at the next block") {
    Project p = quad();
    p.sources = {orbitSource(Period::seconds(1.0))};
    Rig rig(p);
    rig.run(20);
    rig.proc.push(cmd::ResetMotion{1});
    rig.run();
    CHECK(std::abs(Position::wrapAzimuth(rig.pos().azimuth - 3.6f)) < 0.01f);
}

TEST_CASE("Source motion: Manual sources follow SetSourcePosition; other kinds ignore it") {
    Project p = quad();
    SourceSpec manual = makeSource(1, {0.0f, 0.0f, 2.0f});
    manual.motion.value = ManualMotion{{0.0f, 0.0f, 2.0f}, 0.0f};
    SourceSpec orb = orbitSource(Period::seconds(1.0));
    orb.id = 2;
    p.sources = {manual, orb};
    Rig rig(p);
    rig.run();
    rig.proc.push(cmd::SetSourcePosition{1, -1, {90.0f, 0.0f, 2.0f}, 0.0f});
    rig.proc.push(cmd::SetSourcePosition{2, -1, {90.0f, 0.0f, 9.0f}, 0.0f});
    rig.run();
    CHECK(rig.pos(1).azimuth == Approx(90.0f).margin(1e-3));
    CHECK(rig.pos(2).distance == Approx(2.0f).margin(1e-4));
}

TEST_CASE("Source modulators: LFO on motion.radius, external values on position and gain") {
    SECTION("an LFO modulates the orbit radius") {
        Project p = quad();
        SourceSpec s = orbitSource(Period::seconds(100.0));
        ModulatorSpec m;
        m.source = LfoSource{LfoShape::Sine, Period::seconds(0.4)};
        m.target = "motion.radius";
        m.depth = 1.0f;
        s.modulators = {m};
        p.sources = {s};
        Rig rig(p);
        rig.run(10);  // t = 0.1 s: a quarter LFO period, sin = 1
        CHECK(rig.pos().distance == Approx(3.0f).margin(1e-3));
        rig.run(20);  // t = 0.3 s: sin = -1
        CHECK(rig.pos().distance == Approx(1.0f).margin(1e-3));
    }
    SECTION("an OSC modulator value moves a manual source (SetModulatorValue)") {
        Project p = quad();
        SourceSpec s = makeSource(1, {0.0f, 0.0f, 2.0f});
        ModulatorSpec m;
        m.source = OscSource{"/x"};
        m.target = "motion.position.az";
        m.depth = 90.0f;
        s.modulators = {m};
        p.sources = {s};
        Rig rig(p);
        rig.run();
        CHECK(rig.pos().azimuth == Approx(0.0f).margin(1e-3));
        rig.proc.push(cmd::SetModulatorValue{1, 0, 1.0f});
        rig.run();
        CHECK(rig.pos().azimuth == Approx(90.0f).margin(1e-3));
    }
    SECTION("a gain modulator scales the output") {
        Project p = makeProject({makeSpeaker(1, 0.0f, 2.0f, 0)});
        SourceSpec s = makeSource(1, {}, SpatializerKind::Direct);
        s.directSpeakers = {1};
        ModulatorSpec m;
        m.source = MidiCcSource{1};
        m.target = "gain";
        m.depth = -20.0f;  // cc 0..1 → 0..-20 dB
        s.modulators = {m};
        p.sources = {s};
        DiskStreamer streamer;
        EngineProcessor proc;
        GraphBuilder b(p);
        b.setInputFactory([](const SourceSpec&) -> std::unique_ptr<SourceInput> {
            juce::AudioBuffer<float> dc(1, 48000);
            for (int i = 0; i < dc.getNumSamples(); ++i) dc.setSample(0, i, 0.5f);
            return std::make_unique<BufferInput>(std::move(dc));
        });
        proc.setGraphWhileStopped(b.build(kSr, kBlock));
        juce::AudioBuffer<float> out(1, kBlock);
        float* ptrs[] = {out.getWritePointer(0)};
        for (int i = 0; i < 5; ++i) proc.process(nullptr, 0, ptrs, 1, kBlock);
        CHECK(out.getSample(0, kBlock - 1) == Approx(0.5f).margin(1e-4));
        proc.push(cmd::SetModulatorValue{1, 0, 1.0f});
        proc.process(nullptr, 0, ptrs, 1, kBlock);
        // Ramped across the block (seen through the limiter look-ahead): no
        // step, and the target reached one block later.
        CHECK(out.getSample(0, 0) > 0.45f);
        proc.process(nullptr, 0, ptrs, 1, kBlock);
        CHECK(out.getSample(0, kBlock - 1) == Approx(0.05f).margin(1e-4));
    }
}

TEST_CASE("Source motion: phase is carried across SwapGraph") {
    Project p = quad();
    p.sources = {orbitSource(Period::seconds(1.0))};
    Rig rig(p);
    rig.run(10);

    SECTION("unchanged spec: the running motion continues") {
        rig.proc.swapGraph(rig.build(p));
        rig.run(10);
        CHECK(std::abs(Position::wrapAzimuth(rig.pos().azimuth - wrapDeg(3.6 * 20))) < 0.01f);
    }
    SECTION("same kind, edited: the phase continues with the new parameters") {
        Project edited = p;
        std::get<OrbitMotion>(edited.sources[0].motion.value).radius = 3.0f;
        rig.proc.swapGraph(rig.build(edited));
        rig.run(10);
        CHECK(rig.pos().distance == Approx(3.0f).margin(1e-4));
        CHECK(std::abs(Position::wrapAzimuth(rig.pos().azimuth - wrapDeg(3.6 * 20))) < 0.01f);
    }
    SECTION("another kind: a new motion starts at phase 0") {
        Project edited = p;
        LineMotion line;
        line.from = {-90.0f, 0.0f, 2.0f};
        line.to = {90.0f, 0.0f, 2.0f};
        line.period = Period::seconds(1.0);
        line.mode = PlayMode::Once;
        edited.sources[0].motion.value = line;
        rig.proc.swapGraph(rig.build(edited));
        rig.run(1);
        // 10 ms into a 1 s line from -90 to +90 (Cartesian lerp): still near -90.
        CHECK(rig.pos().azimuth < -80.0f);
    }
}

TEST_CASE("Source files: transport commands and FileTransport feedback") {
    const auto dir = juce::File::createTempFile("spacer-transport");
    REQUIRE(dir.createDirectory());
    const auto wav = dir.getChildFile("tone.wav");
    OfflineRenderer::Result tone;
    tone.sampleRate = kSr;
    tone.audio = sine(kSr, 1000.0, 0.5f, 48000);
    REQUIRE_FALSE(OfflineRenderer::writeWav(tone, wav).has_value());

    Project p = makeProject({makeSpeaker(1, 0.0f, 2.0f, 0)});
    SourceSpec s = makeSource(1, {}, SpatializerKind::Direct);
    s.directSpeakers = {1};
    FileInputSpec f;
    f.path = wav.getFullPathName().toStdString();
    s.input.value = f;
    p.sources = {s};
    {
        Rig rig(p, 1);
        rig.run(10);  // stopped: silent
        CHECK(peakAbs(rig.out.getReadPointer(0), kBlock) == 0.0f);
        CHECK_FALSE(rig.lastTransport.playing);
        rig.proc.push(cmd::FilePlay{1});
        rig.run(10);
        CHECK(peakAbs(rig.out.getReadPointer(0), kBlock) > 0.4f);
        rig.proc.push(cmd::FileSeek{1, 0.5});
        rig.run(5);
        rig.proc.push(cmd::FileStop{1});
        rig.run(10);
        CHECK(peakAbs(rig.out.getReadPointer(0), kBlock) == 0.0f);
        CHECK_FALSE(rig.lastTransport.playing);
        // Seek landed at 0.5 s; then 4 blocks played (the seek block is silent).
        CHECK(rig.lastTransport.positionSeconds == Approx(0.5 + 4 * kBlock / kSr).margin(1e-6));
    }
    dir.deleteRecursively();
}

TEST_CASE("Offline render: a file source matches the file (after resampling) within -80 dB") {
    const auto dir = juce::File::createTempFile("spacer-file-source");
    REQUIRE(dir.createDirectory());
    for (double fileRate : {48000.0, 44100.0, 96000.0}) {
        CAPTURE(fileRate);
        const auto wav = dir.getChildFile("tone" + juce::String(static_cast<int>(fileRate)) + ".wav");
        OfflineRenderer::Result tone;
        tone.sampleRate = fileRate;
        tone.audio = sine(fileRate, 997.0, 0.5f, static_cast<int>(fileRate));
        REQUIRE_FALSE(OfflineRenderer::writeWav(tone, wav).has_value());

        Project p = makeProject({makeSpeaker(1, 0.0f, 2.0f, 0)});
        SourceSpec s = makeSource(1, {}, SpatializerKind::Direct);
        s.directSpeakers = {1};
        FileInputSpec f;
        f.path = "tone" + std::to_string(static_cast<int>(fileRate)) + ".wav";
        f.relativeToProject = true;
        s.input.value = f;
        p.sources = {s};
        auto r = OfflineRenderer::render(p, {}, 1.0, {kSr, 128, dir.getFullPathName().toStdString()});
        REQUIRE(r.hasValue());
        CHECK(r->warnings.empty());

        // Output = file at the render rate, delayed by the limiter look-ahead.
        // Skip the source fade-in (10 ms) and the abrupt start of the file's sine.
        const int L = r->latencySamples;
        const float* y = r->audio.getReadPointer(0);
        const int start = L + 960, end = r->audio.getNumSamples() - 960;
        double e = 0.0;
        for (int i = start; i < end; ++i) {
            const double ref = 0.5 * std::sin(2.0 * juce::MathConstants<double>::pi * 997.0 * (i - L) / kSr);
            e += (y[i] - ref) * (y[i] - ref);
        }
        CHECK(10.0 * std::log10(e / (end - start)) < -80.0);
    }
    dir.deleteRecursively();
}

TEST_CASE("Offline render: a missing file is a warning and renders silence") {
    Project p = makeProject({makeSpeaker(1, 0.0f, 2.0f, 0)});
    SourceSpec s = makeSource(1, {}, SpatializerKind::Direct);
    s.directSpeakers = {1};
    FileInputSpec f;
    f.path = "/no/such/file.wav";
    s.input.value = f;
    p.sources = {s};
    auto r = OfflineRenderer::render(p, {}, 0.1);
    REQUIRE(r.hasValue());
    CHECK(r->warnings.size() == 1);
    CHECK(peakAbs(r->audio.getReadPointer(0), r->audio.getNumSamples()) == 0.0f);
}

TEST_CASE("Engine: motion, modulators, generators and files process without allocating") {
    const auto dir = juce::File::createTempFile("spacer-alloc");
    REQUIRE(dir.createDirectory());
    const auto wav = dir.getChildFile("a.wav");
    OfflineRenderer::Result tone;
    tone.sampleRate = 44100.0;
    tone.audio = sine(44100.0, 440.0, 0.5f, 44100);
    REQUIRE_FALSE(OfflineRenderer::writeWav(tone, wav).has_value());
    {
        Project p = quad();
        SourceSpec orb = orbitSource(Period::beats(4.0));
        ModulatorSpec lfo;
        lfo.target = "gain";
        lfo.depth = -3.0f;
        orb.modulators = {lfo};
        SourceSpec file = makeSource(2, {45.0f, 0.0f, 2.0f});
        FileInputSpec f;
        f.path = wav.getFullPathName().toStdString();
        f.loop = true;
        file.input.value = f;
        file.stereo = true;
        SourceSpec steps = makeSource(3, {});
        StepSequenceMotion seq;
        Step a, b;
        a.target = 1;
        b.target = 3;
        seq.steps = {a, b};
        steps.motion.value = seq;
        p.sources = {orb, file, steps};
        Rig rig(p);
        rig.proc.push(cmd::FilePlay{2});
        rig.run(5);

        allocguard::setMode(allocguard::Mode::Count);
        allocguard::resetViolations();
        for (int i = 0; i < 200; ++i) {
            if (i == 50) rig.proc.push(cmd::SetMotionParam{1, -1, MotionParam::Radius, 1.0f});
            if (i == 60) rig.proc.push(cmd::ResetMotion{3});
            if (i == 70) rig.proc.push(cmd::FileSeek{2, 0.3});
            if (i == 80) rig.proc.push(cmd::SetModulatorValue{1, 0, 0.5f});
            rig.run();
        }
        CHECK(allocguard::violations() == 0);
        allocguard::setMode(allocguard::Mode::Assert);
    }
    dir.deleteRecursively();
}

TEST_CASE("Source motion: restartMotion (scene recall restartMotionPhase) resets motion and LFO phase") {
    Project p = quad();
    SourceSpec s = orbitSource(Period::seconds(1.0));
    ModulatorSpec m;
    m.source = LfoSource{LfoShape::Saw, Period::seconds(1.0)};
    m.target = "motion.radius";
    s.modulators = {m};
    p.sources = {s};
    Rig rig(p);
    Rig reference(p);
    reference.run();
    rig.run(37);
    rig.source().restartMotion();
    rig.run();
    CHECK(rig.pos().azimuth == Approx(reference.pos().azimuth).margin(1e-3));
    CHECK(rig.pos().distance == Approx(reference.pos().distance).margin(1e-4));
}

// ---- WP14 -------------------------------------------------------------------------

TEST_CASE("Source motion: a live period change re-anchors the phase (SPEC §8, WP14)") {
    Project p = quad();
    p.sources = {orbitSource(Period::seconds(1.0))};
    Rig rig(p);
    rig.run(25);  // 90 degrees
    const float before = rig.pos().azimuth;
    Project edited = p;
    std::get<OrbitMotion>(edited.sources[0].motion.value).period = Period::seconds(4.0);
    rig.proc.swapGraph(rig.build(edited));
    rig.run(1);
    // Continuous: one 10 ms block further at the new 4 s period (0.9 degrees),
    // not the 25 * 0.9 = 22.5 degrees elapsed time alone would give.
    CHECK(std::abs(Position::wrapAzimuth(rig.pos().azimuth - before) - 0.9f) < 0.05f);
    rig.run(100);  // one more second = 90 degrees at 4 s
    CHECK(std::abs(Position::wrapAzimuth(rig.pos().azimuth - before) - 0.9f - 90.0f) < 0.1f);
}

TEST_CASE("Source modulators: SetModulator edits live without restarting the LFO (WP14)") {
    Project p = quad();
    p.sources = {orbitSource(Period::seconds(1000.0))};
    p.sources[0].modulators = {
        ModulatorSpec{LfoSource{LfoShape::Saw, Period::seconds(1.0)}, "motion.radius", 0.5f, 0.0f}};
    Project reference = p;
    reference.sources[0].modulators[0].depth = 1.0f;
    Rig rig(p), ref(reference);
    rig.run(30);
    ref.run(30);
    ModulatorBank::Live live = ModulatorBank::Live::fromSpec(reference.sources[0].modulators[0]);
    rig.proc.push(cmd::SetModulator{1, 0, 1, live});
    rig.run(7);
    ref.run(7);
    CHECK(rig.source().modulators().value(0) == Approx(ref.source().modulators().value(0)));
    CHECK(rig.pos().distance == Approx(ref.pos().distance).margin(1e-4));
    // Appending a gain modulator live switches gain modulation on.
    ModulatorBank::Live gain;
    gain.lfo = false;
    gain.targetValid = true;
    gain.target = *ParameterPath::parse("gain");
    gain.depth = 1.0f;
    rig.proc.push(cmd::SetModulator{1, 1, 2, gain});
    rig.proc.push(cmd::SetModulatorValue{1, 1, -100.0f});
    rig.run(10);
    CHECK(rig.source().modulators().size() == 2);
    CHECK(peakAbs(rig.out.getReadPointer(0), kBlock) < 1e-3f);
}

TEST_CASE("Source modulators: motionRight.* drives the right point of a linked pair (WP14)") {
    Project p = quad();
    SourceSpec s = orbitSource(Period::seconds(1000.0));
    s.stereo = true;
    s.linkedPoints = true;
    s.input.value = GeneratorSpec{};
    s.motionRight = MotionSpec{orbit(Period::seconds(1000.0), 2.0f)};
    s.modulators = {ModulatorSpec{OscSource{"/r"}, "motionRight.radius", 1.0f, 0.0f},
                    ModulatorSpec{OscSource{"/l"}, "motion.radius", 1.0f, 0.0f}};
    p.sources = {s};
    Rig rig(p);
    rig.proc.push(cmd::SetModulatorValue{1, 0, 1.0f});
    rig.proc.push(cmd::SetModulatorValue{1, 1, 0.5f});
    rig.run(3);
    CHECK(rig.pos(1, 0).distance == Approx(2.5f).margin(1e-3));
    CHECK(rig.pos(1, 1).distance == Approx(3.0f).margin(1e-3));
}
