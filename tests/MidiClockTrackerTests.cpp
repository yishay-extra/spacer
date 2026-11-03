#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <cmath>
#include <cstdint>
#include <deque>

#include "ClockSimUtil.h"
#include "core/clock/MidiClockTracker.h"

using namespace spacer;
using Catch::Approx;

namespace {

constexpr std::uint8_t kTick = 0xF8, kStart = 0xFA, kContinue = 0xFB, kStop = 0xFC;

void send(MidiClockTracker& t, std::uint8_t status, double time) {
    t.handle(&status, 1, time);
}

/// A MIDI clock sender: Start at `startTime`, then ticks at a (possibly
/// changing) tempo, with jittered timestamps. Events become visible to the
/// receiver `latency` after their timestamp.
struct Sender {
    double startTime{100.5};
    double latency{0.001};
    testing::Jitter jitter{0.001, 11};
    std::function<double(double)> bpmAt = [](double) { return 120.0; };
    double clockRatio{1.0};  // sender clock speed vs host (drift)

    struct Event {
        double timestamp;
        std::uint8_t status;
        double trueTime;
    };
    std::deque<Event> pending;
    double nextTick{0.0};
    double beatAtNextTick{0.0};
    bool started{false};

    // Truth: beat at host time t (linear between ticks).
    std::deque<std::pair<double, double>> history;  // (time, beat) per tick

    void generateUntil(double t) {
        if (!started) {
            pending.push_back({startTime, kStart, startTime});
            nextTick = startTime + 0.0005;
            started = true;
        }
        while (nextTick <= t + 0.1) {
            pending.push_back({nextTick + jitter(), kTick, nextTick});
            history.emplace_back(nextTick, beatAtNextTick);
            if (history.size() > 64) history.pop_front();
            const double period = 60.0 / bpmAt(nextTick) / 24.0 / clockRatio;
            nextTick += period;
            beatAtNextTick += 1.0 / 24.0;
        }
    }
    void deliver(MidiClockTracker& tracker, double now) {
        generateUntil(now);
        while (!pending.empty() && pending.front().timestamp + latency <= now) {
            send(tracker, pending.front().status, pending.front().timestamp);
            pending.pop_front();
        }
    }
    double trueBeat(double t) const {
        if (history.empty() || t < history.front().first) return 0.0;
        for (std::size_t i = 1; i < history.size(); ++i)
            if (history[i].first > t) {
                const auto& a = history[i - 1];
                const auto& b = history[i];
                return a.second + (t - a.first) / (b.first - a.first) * (b.second - a.second);
            }
        return history.back().second;
    }
    double trueBpm(double t) const { return bpmAt(t) * clockRatio; }
};

}  // namespace

TEST_CASE("MidiClock: Start begins at the next tick at beat 0", "[clock][midi]") {
    MidiClockTracker t;
    send(t, kStart, 1.0);
    CHECK_FALSE(t.timeline().playing);
    send(t, kTick, 1.01);
    CHECK(t.timeline().playing);
    CHECK(t.timeline().startBeat == 0.0);
    CHECK(t.timeline().anchorBeat == 0.0);
    send(t, kTick, 1.01 + 1.0 / 48.0);
    CHECK(t.timeline().anchorBeat == Approx(1.0 / 24.0));
}

TEST_CASE("MidiClock: ticks advance 1/24 beat each while playing only", "[clock][midi]") {
    MidiClockTracker t;
    const double p = 60.0 / 120.0 / 24.0;
    for (int i = 0; i < 24; ++i) send(t, kTick, 1.0 + i * p);  // stopped: tempo only
    CHECK_FALSE(t.timeline().playing);
    CHECK(t.timeline().anchorBeat == 0.0);
    CHECK(t.bpm() == Approx(120.0).epsilon(1e-6));
    send(t, kStart, 1.0 + 23.5 * p);
    for (int i = 24; i < 24 + 48; ++i) send(t, kTick, 1.0 + i * p);
    CHECK(t.timeline().anchorBeat == Approx(47.0 / 24.0));
    CHECK(t.position() == 48);
}

TEST_CASE("MidiClock: Stop freezes, Continue resumes at the next position", "[clock][midi]") {
    MidiClockTracker t;
    const double p = 60.0 / 120.0 / 24.0;
    send(t, kStart, 1.0);
    for (int i = 0; i < 24; ++i) send(t, kTick, 1.0 + (i + 1) * p);
    send(t, kStop, 1.0 + 24.5 * p);
    CHECK_FALSE(t.timeline().playing);
    const double frozen = t.timeline().beatAt(5.0);
    CHECK(frozen <= 1.0 + 1e-12);
    CHECK(frozen >= 23.0 / 24.0 - 1e-12);
    send(t, kTick, 1.0 + 25 * p);  // clock keeps running while stopped
    CHECK(t.timeline().beatAt(5.0) == frozen);
    send(t, kContinue, 1.0 + 25.5 * p);
    send(t, kTick, 1.0 + 26 * p);
    CHECK(t.timeline().playing);
    CHECK(t.timeline().anchorBeat == Approx(1.0));  // 24 clocks were played
}

TEST_CASE("MidiClock: Song Position Pointer locates while stopped", "[clock][midi]") {
    MidiClockTracker t;
    const std::uint8_t spp[3] = {0xF2, 0x10, 0x01};  // 16 + 128 = 144 sixteenths = 36 beats
    t.handle(spp, 3, 1.0);
    CHECK(t.timeline().anchorBeat == Approx(36.0));
    send(t, kContinue, 1.1);
    send(t, kTick, 1.2);
    CHECK(t.timeline().startBeat == Approx(36.0));
    // Ignored while playing.
    const std::uint8_t spp2[3] = {0xF2, 0x00, 0x00};
    t.handle(spp2, 3, 1.21);
    CHECK(t.timeline().anchorBeat == Approx(36.0));
}

TEST_CASE("MidiClock: locks within two bars with 1 ms jitter", "[clock][midi]") {
    const double bpm = GENERATE(60.0, 120.0, 180.0, 300.0);
    CAPTURE(bpm);
    MidiClockTracker tracker;
    Sender sender;
    sender.bpmAt = [bpm](double) { return bpm; };
    testing::AudioSim sim;
    const double twoBars = 8.0 * 60.0 / bpm;
    double maxErrMs = 0.0, maxBpmErr = 0.0;
    bool lockedInTime = false;
    sim.run(
        sender.startTime - sim.t0 + twoBars * 3.0,
        [&](double now) {
            sender.deliver(tracker, now);
            tracker.update(now);
        },
        [&](double) -> const ClockTimeline& { return tracker.timeline(); },
        [&](const testing::AudioSim::Block& b) {
            const double sinceStart = b.trueTime - sender.startTime;
            if (sinceStart < twoBars) return;
            if (sinceStart < twoBars + 0.01) lockedInTime = tracker.locked();
            REQUIRE(b.state.playing);
            const double errMs = (b.state.beat - sender.trueBeat(b.trueTime)) / (bpm / 60.0) * 1000.0;
            maxErrMs = std::max(maxErrMs, std::abs(errMs));
            maxBpmErr = std::max(maxBpmErr, std::abs(b.state.bpm - bpm) / bpm);
        });
    CHECK(lockedInTime);
    CHECK(maxErrMs < 1.0);
    CHECK(maxBpmErr < 0.005);
}

TEST_CASE("MidiClock: stays within 1 ms over 10 minutes with drift", "[clock][midi][drift]") {
    MidiClockTracker tracker;
    Sender sender;
    sender.clockRatio = 1.0 + 80e-6;  // sender crystal 80 ppm fast
    // Slow tempo wander +-0.05 bpm around 128.
    sender.bpmAt = [](double t) { return 128.0 + 0.05 * std::sin(t * 0.01); };
    testing::AudioSim sim;
    sim.ppm = -40.0;  // audio device 40 ppm slow
    double maxErrMs = 0.0;
    sim.run(
        sender.startTime - sim.t0 + 600.0,
        [&](double now) {
            sender.deliver(tracker, now);
            tracker.update(now);
        },
        [&](double) -> const ClockTimeline& { return tracker.timeline(); },
        [&](const testing::AudioSim::Block& b) {
            if (b.trueTime - sender.startTime < 8.0) return;
            const double bps = sender.trueBpm(b.trueTime) / 60.0;
            const double errMs = (b.state.beat - sender.trueBeat(b.trueTime)) / bps * 1000.0;
            maxErrMs = std::max(maxErrMs, std::abs(errMs));
        });
    CHECK(maxErrMs < 1.0);
    CHECK(tracker.locked());
}

TEST_CASE("MidiClock: relocks within two bars after a tempo jump", "[clock][midi]") {
    MidiClockTracker tracker;
    Sender sender;
    const double jumpAt = sender.startTime + 10.0;
    sender.bpmAt = [jumpAt](double t) { return t < jumpAt ? 120.0 : 150.0; };
    testing::AudioSim sim;
    const double twoBarsAfter = 8.0 * 60.0 / 150.0;
    double maxErrMs = 0.0;
    sim.run(
        jumpAt - sim.t0 + 10.0,
        [&](double now) {
            sender.deliver(tracker, now);
            tracker.update(now);
        },
        [&](double) -> const ClockTimeline& { return tracker.timeline(); },
        [&](const testing::AudioSim::Block& b) {
            if (b.trueTime < jumpAt + twoBarsAfter) return;
            const double errMs = (b.state.beat - sender.trueBeat(b.trueTime)) / 2.5 * 1000.0;
            maxErrMs = std::max(maxErrMs, std::abs(errMs));
            CHECK(b.state.bpm == Approx(150.0).epsilon(0.005));
        });
    CHECK(maxErrMs < 1.0);
}

TEST_CASE("MidiClock: dropout stalls the beat and unlocks", "[clock][midi]") {
    MidiClockTracker t;
    const double p = 60.0 / 120.0 / 24.0;
    send(t, kStart, 1.0);
    int i = 0;
    for (; i < 200; ++i) send(t, kTick, 1.0 + (i + 1) * p);
    CHECK(t.locked());
    const double last = 1.0 + i * p;
    t.update(last + 0.3);
    const double stalled = t.timeline().beatAt(last + 0.3);
    CHECK(stalled < t.timeline().anchorBeat + 4.0 / 24.0);
    t.update(last + 1.0);
    CHECK_FALSE(t.locked());
    CHECK(t.timeline().beatAt(last + 2.0) == Approx(t.timeline().beatAt(last + 1.0)));
    // Clock returns: re-acquires and continues counting.
    for (int k = 0; k < 100; ++k) send(t, kTick, last + 2.0 + k * p);
    CHECK(t.timeline().anchorBeat == Approx((i + 99) / 24.0));
    CHECK(t.locked());
}
