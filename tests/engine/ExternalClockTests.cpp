// WP12: external clocks in the engine — switching through SwapGraph, clock
// hand-over, lock-free input from other threads, OSC receive-thread tap.

#include <atomic>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <memory>
#include <thread>
#include <vector>

#include "EngineTestUtils.h"
#include "control/ClockInputs.h"
#include "control/MidiClock.h"
#include "control/OscServer.h"
#include "engine/AllocationGuard.h"
#include "engine/ClockSelector.h"
#include "engine/EngineProcessor.h"
#include "engine/GraphBuilder.h"

using Catch::Approx;
using namespace spacer;
using namespace spacer::testing;

namespace {

constexpr double kSr = 48000.0;
constexpr int kBlock = 128;

/// Counts blocks; beat = 1000 + blocks, so its state is easy to recognise.
class FakeClock final : public ClockSource {
public:
    explicit FakeClock(bool* destroyed = nullptr) : destroyed_(destroyed) {}
    ~FakeClock() override {
        if (destroyed_) *destroyed_ = true;
    }
    ClockState advance(int) override {
        last_.beat = 1000.0 + blocks_++;
        last_.bpm = 99.0;
        last_.playing = true;
        last_.sampleRate = sampleRate_;
        last_.timeSeconds = -1.0;  // the engine must override this
        return last_;
    }
    ClockState current() const override { return last_; }
    void setSampleRate(double sr) override { sampleRate_ = sr; }
    int blocks_{0};
    double sampleRate_{0.0};

private:
    bool* destroyed_;
    ClockState last_;
};

Project simpleProject() {
    return makeProject({makeSpeaker(1, -30.0f, 2.0f, 0), makeSpeaker(2, 30.0f, 2.0f, 1)},
                       {makeSource(1, {0.0f, 0.0f, 2.0f})});
}

std::unique_ptr<Graph> buildGraph(const Project& p = simpleProject()) {
    return GraphBuilder(p).build(kSr, kBlock);
}

struct Rig {
    EngineProcessor proc;
    juce::AudioBuffer<float> out{2, kBlock};
    Rig() { proc.setGraphWhileStopped(buildGraph()); }
    ClockState run() {
        float* ptrs[] = {out.getWritePointer(0), out.getWritePointer(1)};
        proc.process(nullptr, 0, ptrs, 2, kBlock);
        return proc.clockState();
    }
};

/// Host clock driven by the test (seconds).
std::atomic<double> gNow{100.0};
double fakeNow() {
    return gNow.load();
}

}  // namespace

TEST_CASE("Engine clock: a graph with an external clock makes it the active clock", "[clock][engine]") {
    Rig rig;
    rig.run();
    CHECK_FALSE(rig.proc.externalClockActive());
    auto g = buildGraph();
    auto* fake = new FakeClock();
    g->setClock(std::unique_ptr<ClockSource>(fake));
    REQUIRE(rig.proc.swapGraph(std::move(g)));
    const auto s = rig.run();
    CHECK(rig.proc.externalClockActive());
    CHECK(s.beat == 1000.0);
    CHECK(s.bpm == 99.0);
    CHECK(s.timeSeconds > 0.0);  // engine timeline, not the clock's
    CHECK(fake->sampleRate_ == kSr);
}

TEST_CASE("Engine clock: a rebuilt graph inherits the running clock and its state", "[clock][engine]") {
    Rig rig;
    auto g = buildGraph();
    bool destroyed = false;
    auto* fake = new FakeClock(&destroyed);
    g->setClock(std::unique_ptr<ClockSource>(fake));
    rig.proc.swapGraph(std::move(g));
    for (int i = 0; i < 5; ++i) rig.run();

    REQUIRE(rig.proc.swapGraph(buildGraph()));  // structural edit: no clock set
    const auto s = rig.run();
    CHECK(s.beat == 1005.0);  // the same object continued
    rig.proc.collectGarbage();
    CHECK_FALSE(destroyed);
    CHECK(rig.proc.externalClockActive());
}

TEST_CASE("Engine clock: switching back to internal continues from the external beat", "[clock][engine]") {
    Rig rig;
    auto g = buildGraph();
    bool destroyed = false;
    g->setClock(std::make_unique<FakeClock>(&destroyed));
    rig.proc.swapGraph(std::move(g));
    for (int i = 0; i < 3; ++i) rig.run();  // last external state: beat 1002, playing, 99 bpm

    auto internal = buildGraph();
    internal->setClock(nullptr);
    rig.proc.swapGraph(std::move(internal));
    const auto s = rig.run();
    CHECK_FALSE(rig.proc.externalClockActive());
    CHECK(s.beat == Approx(1002.0));
    CHECK(s.bpm == Approx(99.0));
    CHECK(s.playing);
    const auto s2 = rig.run();
    CHECK(s2.beat == Approx(1002.0 + kBlock / kSr * 99.0 / 60.0));
    // The external clock is deleted with its graph, on the control thread.
    CHECK_FALSE(destroyed);
    rig.proc.collectGarbage();
    CHECK(destroyed);
}

TEST_CASE("Engine clock: a superseded pending graph does not lose a clock switch", "[clock][engine]") {
    Rig rig;
    auto withClock = buildGraph();
    auto* fake = new FakeClock();
    withClock->setClock(std::unique_ptr<ClockSource>(fake));
    rig.proc.swapGraph(std::move(withClock));
    rig.proc.swapGraph(buildGraph());  // queued before the audio thread saw the first
    const auto s = rig.run();
    CHECK(rig.proc.externalClockActive());
    CHECK(s.beat == 1000.0);
}

TEST_CASE("Engine clock: device restart (setGraphWhileStopped) keeps the clock", "[clock][engine]") {
    Rig rig;
    auto g = buildGraph();
    g->setClock(std::make_unique<FakeClock>());
    rig.proc.swapGraph(std::move(g));
    rig.run();
    rig.proc.setGraphWhileStopped(GraphBuilder(simpleProject()).build(44100.0, 256));
    const auto s = rig.run();
    CHECK(s.beat == 1001.0);
    CHECK(s.sampleRate == 44100.0);
}

TEST_CASE("ClockSelector: sets a clock only when the config changes", "[clock][engine]") {
    ClockSelector sel;
    int made = 0;
    sel.setFactory([&](const ClockConfig&) -> std::unique_ptr<ClockSource> {
        ++made;
        return std::make_unique<FakeClock>();
    });
    ClockConfig internal;
    auto g1 = buildGraph();
    CHECK_FALSE(sel.apply(*g1, internal));  // a fresh engine already runs it
    CHECK_FALSE(g1->clockSet());

    ClockConfig midi;
    midi.kind = ClockKind::Midi;
    auto g2 = buildGraph();
    CHECK(sel.apply(*g2, midi));
    CHECK(g2->clock() != nullptr);
    CHECK(sel.activeKind() == ClockKind::Midi);
    auto g3 = buildGraph();
    CHECK_FALSE(sel.apply(*g3, midi));  // inherits
    CHECK_FALSE(g3->clockSet());
    CHECK(made == 1);

    auto g4 = buildGraph();
    CHECK(sel.apply(*g4, internal));
    CHECK(g4->clockSet());
    CHECK(g4->clock() == nullptr);

    sel.invalidate();
    auto g5 = buildGraph();
    CHECK(sel.apply(*g5, internal));
    CHECK(g5->clockSet());
}

TEST_CASE("ClockSelector: an unavailable clock kind falls back to internal", "[clock][engine]") {
    ClockSelector sel;
    sel.setFactory([](const ClockConfig&) -> std::unique_ptr<ClockSource> { return nullptr; });
    ClockConfig link;
    link.kind = ClockKind::Link;
    auto g = buildGraph();
    CHECK(sel.apply(*g, link));
    CHECK(g->clockSet());
    CHECK(g->clock() == nullptr);
    CHECK(sel.activeKind() == ClockKind::Internal);
}

TEST_CASE("ClockInputs: queues clock data only while a clock of that kind exists", "[clock][control]") {
    ClockInputs inputs;
    const std::uint8_t tick = 0xF8, note[3] = {0x90, 60, 100}, qf[2] = {0xF1, 0x00};
    CHECK(inputs.handleMidi(&tick, 1, 1.0));
    CHECK(inputs.handleMidi(qf, 2, 1.0));
    CHECK_FALSE(inputs.handleMidi(note, 3, 1.0));
    CHECK_FALSE(inputs.handleOscBeat(1.0, 1.0));  // no OscClock: goes to the internal clock

    ClockConfig cfg;
    cfg.kind = ClockKind::Osc;
    auto osc = inputs.makeClock(cfg);
    REQUIRE(osc);
    CHECK(inputs.oscClockActive());
    CHECK(inputs.handleOscBeat(1.0, 1.0));
    osc.reset();
    CHECK_FALSE(inputs.oscClockActive());

    cfg.kind = ClockKind::Internal;
    CHECK(inputs.makeClock(cfg) == nullptr);
    cfg.kind = ClockKind::Link;
    CHECK((inputs.makeClock(cfg) != nullptr) == ClockInputs::linkAvailable());
}

TEST_CASE("MidiClock in the engine: fed from another thread, locks, no audio-thread allocation",
          "[clock][engine][control]") {
    ClockInputs inputs;
    ClockConfig cfg;
    cfg.kind = ClockKind::Midi;
    gNow = 100.0;
    Rig rig;
    auto g = buildGraph();
    g->setClock(inputs.makeClock(cfg, fakeNow));
    rig.proc.swapGraph(std::move(g));

    // "MIDI thread": Start, then 120 bpm clock, timestamped, pushed as host
    // time passes. Host time advances by one block per audio block.
    const double tickPeriod = 60.0 / 120.0 / 24.0;
    std::atomic<bool> done{false};
    std::atomic<int> sentTicks{0};
    std::thread midi([&] {
        const std::uint8_t start = 0xFA, tick = 0xF8;
        const double t0 = 100.05;
        bool started = false;
        int n = 0;
        while (!done.load()) {
            const double now = gNow.load();
            if (!started && now >= t0) {
                inputs.handleMidi(&start, 1, t0);
                started = true;
            }
            while (started && t0 + 0.001 + n * tickPeriod <= now) {
                inputs.handleMidi(&tick, 1, t0 + 0.001 + n * tickPeriod);
                sentTicks = ++n;
            }
            std::this_thread::yield();
        }
    });

    allocguard::setMode(allocguard::Mode::Count);
    allocguard::resetViolations();
    bool sawStart = false;
    ClockState s;
    const int blocks = static_cast<int>(6.0 * kSr / kBlock);  // 6 s = 3 bars at 120 bpm
    for (int i = 0; i < blocks; ++i) {
        // Wait (bounded) until the "MIDI thread" has caught up with host time.
        const int expected = static_cast<int>((gNow.load() - 100.051) / tickPeriod);
        for (int spin = 0; spin < 100000 && sentTicks.load() < expected; ++spin) std::this_thread::yield();
        s = rig.run();
        if (s.startOffset >= 0) sawStart = true;
        gNow = gNow.load() + kBlock / kSr;
    }
    CHECK(allocguard::violations() == 0);
    done = true;
    midi.join();

    CHECK(sawStart);
    CHECK(s.playing);
    CHECK(s.bpm == Approx(120.0).epsilon(0.002));
    const double expectedBeat = (gNow.load() - kBlock / kSr - 100.051) * 2.0;
    CHECK(s.beat == Approx(expectedBeat).margin(0.01));
    auto* clock = dynamic_cast<MidiClock*>(rig.proc.currentGraph()->clock());
    REQUIRE(clock);
    CHECK(clock->locked());
}

TEST_CASE("OscServer: clock tap sees /clock/beat on the receiver thread with host time", "[clock][control]") {
    OscServer server;
    std::vector<double> beats;
    bool consume = true;
    server.setClockTap([&](const ControlEvent& e, double hostTime) {
        CHECK(hostTime > 0.0);
        if (const auto* b = std::get_if<ev::ClockBeat>(&e)) {
            beats.push_back(b->beat);
            return consume;
        }
        return false;
    });
    server.handleMessage(juce::OSCMessage(juce::OSCAddressPattern("/spacer/clock/beat"), 3.5f));
    server.handleMessage(juce::OSCMessage(juce::OSCAddressPattern("/spacer/clock/stop")));
    std::vector<ControlEvent> got;
    server.pump(1e12, [&](const ControlEvent& e) { got.push_back(e); });
    REQUIRE(beats.size() == 1);
    CHECK(beats[0] == 3.5);
    REQUIRE(got.size() == 1);  // the beat was consumed, stop still reaches the dispatcher
    CHECK(std::holds_alternative<ev::ClockStop>(got[0]));

    consume = false;  // no OscClock active: the beat goes on to the internal clock
    server.handleMessage(juce::OSCMessage(juce::OSCAddressPattern("/spacer/clock/beat"), 4.0f));
    got.clear();
    server.pump(1e12, [&](const ControlEvent& e) { got.push_back(e); });
    REQUIRE(got.size() == 1);
    CHECK(std::holds_alternative<ev::ClockBeat>(got[0]));
}

TEST_CASE("ClockInputs: compensation = device output + engine latency + trim (WP14)", "[clock][control]") {
    CHECK(ClockInputs::compensationSeconds(480, 72, 48000.0, 0.0f) == Approx((480.0 + 72.0) / 48000.0));
    CHECK(ClockInputs::compensationSeconds(480, 72, 48000.0, 5.0f) == Approx(552.0 / 48000.0 + 0.005));
    CHECK(ClockInputs::compensationSeconds(0, 0, 48000.0, -50.0f) == 0.0);  // never negative
    CHECK(ClockInputs::compensationSeconds(480, 0, 0.0, 0.0f) == 0.0);      // no device yet
}

TEST_CASE("External clocks report the beat at the time the block is heard (WP14)", "[clock][control]") {
    // Two MIDI clocks fed the same 120 bpm clock; one compensates 100 ms.
    ClockInputs plain, compensated;
    compensated.setCompensation(0.1);
    REQUIRE(compensated.compensation() == Approx(0.1));
    ClockConfig cfg;
    cfg.kind = ClockKind::Midi;
    double now = 10.0;
    const auto host = [&now] { return now; };
    auto a = plain.makeClock(cfg, host);
    auto b = compensated.makeClock(cfg, host);
    a->setSampleRate(kSr);
    b->setSampleRate(kSr);
    const std::uint8_t start = 0xFA, tick = 0xF8;
    const double t0 = 10.01, tickPeriod = 60.0 / 120.0 / 24.0;
    plain.handleMidi(&start, 1, t0);
    compensated.handleMidi(&start, 1, t0);
    int n = 0;
    ClockState sa, sb;
    for (int i = 0; i < static_cast<int>(8.0 * kSr / kBlock); ++i) {
        while (t0 + 0.001 + n * tickPeriod <= now) {
            plain.handleMidi(&tick, 1, t0 + 0.001 + n * tickPeriod);
            compensated.handleMidi(&tick, 1, t0 + 0.001 + n * tickPeriod);
            ++n;
        }
        sa = a->advance(kBlock);
        sb = b->advance(kBlock);
        now += kBlock / kSr;
    }
    REQUIRE(sa.playing);
    REQUIRE(sb.playing);
    CHECK(sb.beat - sa.beat == Approx(0.1 * 2.0).margin(0.01));  // 100 ms at 2 beats/s
}

TEST_CASE("ClockSelector: a latency trim change does not recreate the clock (WP14)", "[clock][engine]") {
    int made = 0;
    ClockSelector sel;
    sel.setFactory([&made](const ClockConfig&) -> std::unique_ptr<ClockSource> {
        ++made;
        return std::make_unique<FakeClock>();
    });
    ClockConfig cfg;
    cfg.kind = ClockKind::Midi;
    auto g = buildGraph();
    CHECK(sel.apply(*g, cfg));
    cfg.latencyTrimMs = 10.0f;
    auto g2 = buildGraph();
    CHECK_FALSE(sel.apply(*g2, cfg));
    CHECK(made == 1);
}
