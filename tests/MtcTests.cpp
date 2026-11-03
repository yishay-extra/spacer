#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <cmath>
#include <cstdint>
#include <vector>

#include "ClockSimUtil.h"
#include "core/clock/MtcDecoder.h"
#include "core/clock/MtcTracker.h"
#include "core/clock/Timecode.h"

using namespace spacer;
using Catch::Approx;

namespace {

/// The 8 quarter-frame data bytes for `tc`.
std::vector<std::uint8_t> quarterFrames(const Timecode& tc) {
    const int rate = static_cast<int>(tc.rate);
    const int v[8] = {
        tc.frames & 0xF,  (tc.frames >> 4) & 0x1,  tc.seconds & 0xF, (tc.seconds >> 4) & 0x3,
        tc.minutes & 0xF, (tc.minutes >> 4) & 0x3, tc.hours & 0xF,   ((tc.hours >> 4) & 0x1) | (rate << 1)};
    std::vector<std::uint8_t> out;
    for (int i = 0; i < 8; ++i) out.push_back(static_cast<std::uint8_t>((i << 4) | v[i]));
    return out;
}

Timecode fromFrameNumber(long long frameNumber, TimecodeRate rate) {
    // Inverse of Timecode::toSeconds() frame counting (drop-frame aware).
    const int fps = Timecode::frameCount(rate);
    if (rate == TimecodeRate::Fps2997Drop) {
        const long long d = frameNumber / 17982, m = frameNumber % 17982;
        frameNumber += 18 * d + (m > 1 ? 2 * ((m - 2) / 1798) : 0);
    }
    Timecode tc;
    tc.rate = rate;
    tc.frames = static_cast<int>(frameNumber % fps);
    tc.seconds = static_cast<int>((frameNumber / fps) % 60);
    tc.minutes = static_cast<int>((frameNumber / (fps * 60)) % 60);
    tc.hours = static_cast<int>(frameNumber / (fps * 3600LL));
    return tc;
}

/// MTC sender: quarter frames from `startSeconds` at real time, with jitter.
struct MtcSender {
    TimecodeRate rate{TimecodeRate::Fps25};
    double startTime{100.5};
    double startSeconds{3600.0};
    double latency{0.001};
    testing::Jitter jitter{0.001, 5};
    long long qfIndex{0};
    struct Ev {
        double ts;
        std::uint8_t data;
    };
    std::vector<Ev> pending;
    std::size_t next{0};

    double fps() const { return Timecode::framesPerSecond(rate); }
    void generateUntil(double t) {
        while (startTime + static_cast<double>(qfIndex) * 0.25 / fps() <= t + 0.05) {
            const double sendTime = startTime + static_cast<double>(qfIndex) * 0.25 / fps();
            const long long frame0 = std::llround(startSeconds * fps()) + 2 * (qfIndex / 8);
            const auto qfs = quarterFrames(fromFrameNumber(frame0, rate));
            pending.push_back({sendTime + jitter(), qfs[static_cast<std::size_t>(qfIndex % 8)]});
            ++qfIndex;
        }
    }
    void deliver(MtcTracker& tracker, double now) {
        generateUntil(now);
        while (next < pending.size() && pending[next].ts + latency <= now) {
            const std::uint8_t msg[2] = {0xF1, pending[next].data};
            tracker.handle(msg, 2, pending[next].ts);
            ++next;
        }
    }
    double trueSeconds(double t) const {
        return std::llround(startSeconds * fps()) / fps() + (t - startTime);
    }
};

}  // namespace

TEST_CASE("Timecode: seconds for every rate", "[clock][mtc]") {
    Timecode tc{1, 2, 3, 12, TimecodeRate::Fps25};
    CHECK(tc.toSeconds() == Approx(3723.0 + 12.0 / 25.0));
    tc.rate = TimecodeRate::Fps24;
    CHECK(tc.toSeconds() == Approx(3723.0 + 12.0 / 24.0));
    tc.rate = TimecodeRate::Fps30;
    CHECK(tc.toSeconds() == Approx(3723.0 + 12.0 / 30.0));
}

TEST_CASE("Timecode: drop frame skips frames 0 and 1 except every tenth minute", "[clock][mtc]") {
    // 00:01:00;02 is the frame right after 00:00:59;29.
    Timecode a{0, 0, 59, 29, TimecodeRate::Fps2997Drop};
    Timecode b{0, 1, 0, 2, TimecodeRate::Fps2997Drop};
    const double frame = 1001.0 / 30000.0;
    CHECK(b.toSeconds() - a.toSeconds() == Approx(frame));
    // Ten minutes of drop frame = 17982 frames = 600 real seconds (to 1 frame).
    Timecode ten{0, 10, 0, 0, TimecodeRate::Fps2997Drop};
    CHECK(ten.toSeconds() == Approx(17982.0 * frame));
    CHECK(std::abs(ten.toSeconds() - 600.0) < 0.001);
}

TEST_CASE("MtcDecoder: assembles a full quarter-frame set", "[clock][mtc]") {
    const auto rate =
        GENERATE(TimecodeRate::Fps24, TimecodeRate::Fps25, TimecodeRate::Fps2997Drop, TimecodeRate::Fps30);
    const Timecode tc{17, 42, 33, 21, rate};
    MtcDecoder d;
    const auto qf = quarterFrames(tc);
    for (int i = 0; i < 7; ++i)
        CHECK(d.quarterFrame(qf[static_cast<std::size_t>(i)]).kind == MtcDecoder::Result::Kind::QuarterFrame);
    const auto r = d.quarterFrame(qf[7]);
    REQUIRE(r.kind == MtcDecoder::Result::Kind::FullSet);
    CHECK(r.timecode == tc);
}

TEST_CASE("MtcDecoder: out-of-order pieces discard the set", "[clock][mtc]") {
    MtcDecoder d;
    const auto qf = quarterFrames({1, 0, 0, 0, TimecodeRate::Fps25});
    for (int i = 0; i < 4; ++i) d.quarterFrame(qf[static_cast<std::size_t>(i)]);
    d.quarterFrame(qf[5]);  // piece 4 lost
    d.quarterFrame(qf[6]);
    CHECK(d.quarterFrame(qf[7]).kind == MtcDecoder::Result::Kind::QuarterFrame);
    // Starting mid-set (joining a running stream) also waits for piece 0.
    MtcDecoder e;
    for (int i = 3; i < 8; ++i)
        CHECK(e.quarterFrame(qf[static_cast<std::size_t>(i)]).kind == MtcDecoder::Result::Kind::QuarterFrame);
    for (int i = 0; i < 7; ++i) e.quarterFrame(qf[static_cast<std::size_t>(i)]);
    CHECK(e.quarterFrame(qf[7]).kind == MtcDecoder::Result::Kind::FullSet);
}

TEST_CASE("MtcDecoder: full-frame SysEx", "[clock][mtc]") {
    const std::uint8_t msg[10] = {0xF0, 0x7F, 0x7F, 0x01, 0x01, static_cast<std::uint8_t>((2 << 5) | 5),
                                  30,   15,   7,    0xF7};
    MtcDecoder d;
    const auto r = d.handle(msg, 10);
    REQUIRE(r.kind == MtcDecoder::Result::Kind::FullFrame);
    CHECK(r.timecode == Timecode{5, 30, 15, 7, TimecodeRate::Fps2997Drop});
    const std::uint8_t other[10] = {0xF0, 0x7E, 0x7F, 0x01, 0x01, 0, 0, 0, 0, 0xF7};
    CHECK(d.handle(other, 10).kind == MtcDecoder::Result::Kind::None);
}

TEST_CASE("MtcTracker: beat = timecode seconds at the configured tempo", "[clock][mtc]") {
    MtcTracker t(90.0);
    const std::uint8_t msg[10] = {0xF0, 0x7F, 0x7F, 0x01, 0x01, static_cast<std::uint8_t>(1 << 5),
                                  0,    10,   0,    0xF7};
    t.handle(msg, 10, 1.0);
    CHECK_FALSE(t.timeline().playing);
    CHECK(t.timeline().anchorBeat == Approx(10.0 * 90.0 / 60.0));
}

TEST_CASE("MtcTracker: follows within 1 ms with jitter, every rate", "[clock][mtc]") {
    const auto rate =
        GENERATE(TimecodeRate::Fps24, TimecodeRate::Fps25, TimecodeRate::Fps2997Drop, TimecodeRate::Fps30);
    CAPTURE(static_cast<int>(rate));
    MtcTracker tracker(120.0);
    MtcSender sender;
    sender.rate = rate;
    testing::AudioSim sim;
    double maxErrMs = 0.0;
    bool sawStart = false;
    sim.run(
        sender.startTime - sim.t0 + 30.0,
        [&](double now) {
            sender.deliver(tracker, now);
            tracker.update(now);
        },
        [&](double) -> const ClockTimeline& { return tracker.timeline(); },
        [&](const testing::AudioSim::Block& b) {
            if (b.state.startOffset >= 0) sawStart = true;
            if (b.trueTime - sender.startTime < 1.0) return;
            REQUIRE(b.state.playing);
            const double errMs = (b.state.beat / 2.0 - sender.trueSeconds(b.trueTime)) * 1000.0;
            maxErrMs = std::max(maxErrMs, std::abs(errMs));
        });
    CHECK(sawStart);
    CHECK(tracker.timeline().locked);
    CHECK(maxErrMs < 1.0);
}

TEST_CASE("MtcTracker: stops after the timeout and relocates on a jump", "[clock][mtc]") {
    MtcTracker t(120.0);
    const double qf = 0.25 / 25.0;
    double time = 1.0;
    auto feed = [&](const Timecode& tc) {
        for (auto b : quarterFrames(tc)) {
            const std::uint8_t m[2] = {0xF1, b};
            t.handle(m, 2, time);
            time += qf;
        }
    };
    feed({0, 0, 10, 0, TimecodeRate::Fps25});
    feed({0, 0, 10, 2, TimecodeRate::Fps25});
    CHECK(t.running());
    const auto epoch = t.timeline().epoch;
    feed({0, 5, 0, 0, TimecodeRate::Fps25});  // locate to 5 minutes
    CHECK(t.timeline().epoch == epoch + 1);
    CHECK(t.seconds() == Approx(300.0 + 7.0 * qf));
    t.update(time + 0.2);
    CHECK_FALSE(t.running());
    CHECK_FALSE(t.timeline().playing);
}
