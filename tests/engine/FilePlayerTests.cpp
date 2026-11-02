#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cmath>
#include <future>
#include <thread>
#include <vector>

#include "TestReaders.h"
#include "engine/AllocationGuard.h"
#include "engine/DiskStreamer.h"
#include "engine/FilePlayer.h"

using Catch::Approx;
using namespace spacer;
using namespace spacer::testing;

namespace {

constexpr double kSr = 48000.0;
constexpr int kBlock = 128;

/// A player on a synthetic reader, serviced by hand (no disk thread).
struct PlayerRig {
    DiskStreamer streamer;
    FilePlayer player;
    int channels;

    PlayerRig(std::unique_ptr<juce::AudioFormatReader> reader, FileInputSpec spec = {}, int numChannels = 1,
              bool force = false)
        : player(spec, numChannels, std::move(reader), &streamer, "test", force), channels(numChannels) {
        player.prepare(kSr, kBlock);
    }

    /// Reads n samples (one block) after servicing the disk side.
    std::vector<float> block(int n = kBlock, int channel = 0, bool service = true) {
        if (service) streamer.serviceAll();
        std::vector<float> a(static_cast<std::size_t>(n)), b(static_cast<std::size_t>(n));
        float* d[] = {a.data(), b.data()};
        player.read({}, d, channels, n);
        return channel == 0 ? a : b;
    }
    std::vector<float> run(int blocks, int channel = 0) {
        std::vector<float> all;
        for (int i = 0; i < blocks; ++i) {
            auto b = block(kBlock, channel);
            all.insert(all.end(), b.begin(), b.end());
        }
        return all;
    }
};

std::unique_ptr<RampReader> ramp(std::int64_t length, double rate = kSr, int channels = 1) {
    return std::make_unique<RampReader>(rate, length, channels);
}

/// RMS of (x − reference), in dBFS.
double errorDb(const std::vector<float>& x, int start, int n, const std::function<double(int)>& reference) {
    double e = 0.0;
    for (int i = start; i < start + n; ++i) {
        const double d = x[static_cast<std::size_t>(i)] - reference(i);
        e += d * d;
    }
    return 10.0 * std::log10(e / n + 1e-30);
}

}  // namespace

TEST_CASE("FilePlayer: starts stopped, then plays the file sample for sample at the engine rate") {
    PlayerRig rig(ramp(10000));
    for (float v : rig.block()) CHECK(v == 0.0f);
    rig.player.play();
    const auto out = rig.run(20);
    for (int i = 0; i < 2560; ++i) REQUIRE(out[static_cast<std::size_t>(i)] == RampReader::valueOf(i));
}

TEST_CASE("FilePlayer: stop holds the position and play continues exactly there") {
    PlayerRig rig(ramp(10000));
    rig.player.play();
    rig.run(3);
    rig.player.stop();
    for (float v : rig.run(5)) CHECK(v == 0.0f);
    CHECK(rig.player.positionSeconds() == Approx(3 * kBlock / kSr));
    rig.player.play();
    const auto out = rig.block();
    CHECK(RampReader::frameOf(out[0]) == 3 * kBlock);
    CHECK(RampReader::frameOf(out[kBlock - 1]) == 4 * kBlock - 1);
}

TEST_CASE("FilePlayer: startSeconds sets the first frame") {
    FileInputSpec spec;
    spec.startSeconds = 0.5f;
    PlayerRig rig(ramp(48000), spec);
    rig.player.play();
    CHECK(RampReader::frameOf(rig.block()[0]) == 24000);
}

TEST_CASE("FilePlayer: seek is sample-accurate") {
    PlayerRig rig(ramp(100000));
    rig.player.play();
    rig.run(4);
    rig.player.seek(12345.0 / kSr);
    // The block in which the seek arrives is silent (buffered audio is
    // discarded and the disk side has not refilled yet)…
    for (float v : rig.block(kBlock, 0, /*service*/ false)) CHECK(v == 0.0f);
    // …then playback resumes exactly at the requested frame, contiguously.
    const auto out = rig.run(10);
    for (int i = 0; i < 10 * kBlock; ++i)
        REQUIRE(RampReader::frameOf(out[static_cast<std::size_t>(i)]) == 12345 + i);
    CHECK(rig.player.positionSeconds() == Approx((12345 + 10 * kBlock) / kSr));

    SECTION("seeking while stopped positions without playing") {
        rig.player.stop();
        rig.player.seek(1.0);
        for (float v : rig.run(2)) CHECK(v == 0.0f);
        rig.player.play();
        CHECK(RampReader::frameOf(rig.block()[0]) == 48000);
    }
    SECTION("several seeks in a row: the last one wins") {
        rig.player.seek(0.1);
        rig.streamer.serviceAll();
        rig.player.seek(0.2);
        rig.player.seek(0.3);
        rig.block(kBlock, 0, false);
        CHECK(RampReader::frameOf(rig.block()[0]) == 14400);
    }
}

TEST_CASE("FilePlayer: loop is sample-accurate and repeats from startSeconds") {
    FileInputSpec spec;
    spec.loop = true;
    SECTION("whole file") {
        PlayerRig rig(ramp(1000), spec);
        rig.player.play();
        const auto out = rig.run(40);  // > 5 passes
        for (int i = 0; i < 40 * kBlock; ++i)
            REQUIRE(RampReader::frameOf(out[static_cast<std::size_t>(i)]) == i % 1000);
        CHECK(rig.player.positionSeconds() == Approx((40 * kBlock % 1000) / kSr));
    }
    SECTION("loop region [startSeconds, end)") {
        spec.startSeconds = static_cast<float>(200.0 / kSr);
        PlayerRig rig(ramp(1000), spec);
        rig.player.play();
        const auto out = rig.run(20);
        for (int i = 0; i < 20 * kBlock; ++i)
            REQUIRE(RampReader::frameOf(out[static_cast<std::size_t>(i)]) == 200 + i % 800);
    }
}

TEST_CASE("FilePlayer: a non-looping file stops at its last frame; play restarts it") {
    PlayerRig rig(ramp(1000));
    rig.player.play();
    const auto out = rig.run(10);
    for (int i = 0; i < 1000; ++i) REQUIRE(RampReader::frameOf(out[static_cast<std::size_t>(i)]) == i);
    for (std::size_t i = 1000; i < out.size(); ++i) REQUIRE(out[i] == 0.0f);
    CHECK_FALSE(rig.player.playing());
    CHECK(rig.player.atEnd());
    CHECK(rig.player.underruns() == 0);
    rig.player.play();
    rig.block(kBlock, 0, false);  // the restart is a seek
    CHECK(RampReader::frameOf(rig.block()[0]) == 0);
}

TEST_CASE("FilePlayer: gainDb, mono/stereo mapping") {
    SECTION("gain") {
        FileInputSpec spec;
        spec.gainDb = -6.0f;
        PlayerRig rig(ramp(10000), spec);
        rig.player.play();
        const auto out = rig.block();
        CHECK(out[100] == Approx(RampReader::valueOf(100) * juce::Decibels::decibelsToGain(-6.0f)));
    }
    SECTION("stereo file into a stereo source keeps channels") {
        PlayerRig rig(ramp(10000, kSr, 2), {}, 2);
        rig.player.play();
        rig.streamer.serviceAll();
        std::vector<float> l(kBlock), r(kBlock);
        float* d[] = {l.data(), r.data()};
        rig.player.read({}, d, 2, kBlock);
        CHECK(l[10] == RampReader::valueOf(10));
        CHECK(r[10] == -RampReader::valueOf(10));
    }
    SECTION("stereo file into a mono source is averaged") {
        PlayerRig rig(ramp(10000, kSr, 2), {}, 1);
        rig.player.play();
        CHECK(rig.block()[10] == 0.0f);
    }
    SECTION("mono file into a stereo source goes to both sides") {
        PlayerRig rig(ramp(10000), {}, 2);
        rig.player.play();
        CHECK(rig.block(kBlock, 1)[10] == RampReader::valueOf(10));
    }
}

TEST_CASE("FilePlayer: a resampled file matches the file within -80 dB") {
    const double hz = 1000.0, amp = 0.5;
    const auto reference = [&](double t) {
        return amp * std::sin(2.0 * juce::MathConstants<double>::pi * hz * t);
    };
    for (double fileRate : {44100.0, 96000.0, 32000.0, 88200.0}) {
        CAPTURE(fileRate);
        PlayerRig rig(
            std::make_unique<SineReader>(fileRate, hz, amp, static_cast<std::int64_t>(fileRate * 2)));
        REQUIRE(rig.player.stream()->ratio() == Approx(fileRate / kSr));
        rig.player.play();
        const auto out = rig.run(300);  // 0.8 s, > ring size: the disk side keeps up
        // Skip the first 1 ms: the file's sine starts abruptly at frame 0.
        CHECK(errorDb(out, 48, static_cast<int>(out.size()) - 48, [&](int i) { return reference(i / kSr); }) <
              -80.0);

        SECTION("and a seek lands at the requested file time") {
            rig.player.seek(0.25);
            rig.block(kBlock, 0, false);
            const auto after = rig.run(50);
            CHECK(errorDb(after, 0, static_cast<int>(after.size()),
                          [&](int i) { return reference(0.25 + i / kSr); }) < -80.0);
        }
    }
}

TEST_CASE("FilePlayer: a resampled loop has no gap at the loop point") {
    // 441 frames at 44.1 kHz = exactly 10 ms = 10 periods of 1 kHz: a seamless
    // loop of the file is a continuous sine.
    FileInputSpec spec;
    spec.loop = true;
    PlayerRig rig(std::make_unique<SineReader>(44100.0, 1000.0, 0.5, 441), spec);
    rig.player.play();
    const auto out = rig.run(40);
    CHECK(errorDb(out, 48, static_cast<int>(out.size()) - 48, [](int i) {
              return 0.5 * std::sin(2.0 * juce::MathConstants<double>::pi * 1000.0 * i / kSr);
          }) < -80.0);
}

TEST_CASE("FilePlayer: the interpolation path at ratio 1 is aligned to the frame") {
    PlayerRig rig(std::make_unique<SineReader>(kSr, 1000.0, 0.5, 96000), {}, 1, /*force*/ true);
    rig.player.play();
    const auto out = rig.run(50);
    CHECK(errorDb(out, 48, static_cast<int>(out.size()) - 48, [](int i) {
              return 0.5 * std::sin(2.0 * juce::MathConstants<double>::pi * 1000.0 * i / kSr);
          }) < -100.0);
}

TEST_CASE("FilePlayer: underruns play silence, are counted, and skip no audio") {
    PlayerRig rig(ramp(200000));
    rig.player.play();
    // Without servicing, the primed ring (1 s) runs dry.
    std::vector<float> all;
    for (int i = 0; i < 400; ++i) {
        auto b = rig.block(kBlock, 0, false);
        all.insert(all.end(), b.begin(), b.end());
    }
    CHECK(rig.player.underruns() > 0);
    const auto played = static_cast<std::int64_t>(rig.player.positionSeconds() * kSr + 0.5);
    rig.streamer.serviceAll();
    CHECK(RampReader::frameOf(rig.block(kBlock, 0, false)[0]) == played);
}

TEST_CASE("FilePlayer: graph swap takes over the running stream without a gap") {
    DiskStreamer streamer;
    FileInputSpec spec;
    FilePlayer a(spec, 1, ramp(100000), &streamer, "same");
    a.prepare(kSr, kBlock);
    a.play();
    std::vector<float> buf(kBlock);
    float* d[] = {buf.data()};
    for (int i = 0; i < 5; ++i) {
        streamer.serviceAll();
        a.read({}, d, 1, kBlock);
    }
    SECTION("same file") {
        spec.loop = true;  // a setting change does not break continuity
        FilePlayer b(spec, 1, ramp(100000), &streamer, "same");
        b.prepare(kSr, kBlock);
        b.takeStateFrom(a);
        CHECK(b.playing());
        b.read({}, d, 1, kBlock);
        CHECK(RampReader::frameOf(buf[0]) == 5 * kBlock);
        CHECK(b.stream()->loop());
    }
    SECTION("another file starts afresh") {
        FilePlayer b(spec, 1, ramp(100000), &streamer, "other");
        b.prepare(kSr, kBlock);
        b.takeStateFrom(a);
        CHECK_FALSE(b.playing());
    }
    SECTION("same file at another engine rate continues from the same file time") {
        FilePlayer b(spec, 1, ramp(100000), &streamer, "same");
        b.prepare(96000.0, kBlock);
        b.takeStateFrom(a);
        CHECK(b.playing());
        CHECK(b.positionSeconds() == Approx(5 * kBlock / kSr));
    }
}

TEST_CASE("FilePlayer: read and transport do not allocate") {
    PlayerRig rig(ramp(100000));
    allocguard::setMode(allocguard::Mode::Count);
    allocguard::resetViolations();
    std::vector<float> buf(kBlock);
    float* d[] = {buf.data()};
    {
        allocguard::ScopedAudioThread audio;
        rig.player.play();
        for (int i = 0; i < 10; ++i) rig.player.read({}, d, 1, kBlock);
        rig.player.seek(0.5);
        rig.player.read({}, d, 1, kBlock);
        rig.player.stop();
        TransportState t;
        rig.player.transportState(t);
    }
    CHECK(allocguard::violations() == 0);
    allocguard::setMode(allocguard::Mode::Assert);
}

TEST_CASE("DiskStreamer: a stalled reader never blocks the audio thread, nor does stopping the thread") {
    auto readerPtr = std::make_unique<StallingReader>(kSr, 10 * 48000);
    StallingReader* reader = readerPtr.get();
    PlayerRig rig(std::move(readerPtr));  // primed before the stall
    rig.streamer.start();
    REQUIRE(rig.streamer.running());

    reader->stalled = true;
    rig.player.play();
    // Play through the primed second and well beyond while the disk thread
    // is stuck inside readSamples().
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (reader->readsInProgress.load() == 0 && std::chrono::steady_clock::now() < deadline) {
        rig.block(kBlock, 0, false);  // make room in the ring, so the disk thread reads
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE(reader->readsInProgress.load() == 1);

    // Stop the disk thread from another ("message") thread: it has to wait
    // for the stalled read…
    auto stopped = std::async(std::launch::async, [&] { rig.streamer.stop(); });

    // …while the audio thread keeps running in bounded time.
    double worstMs = 0.0;
    for (int i = 0; i < 800; ++i) {
        const auto t0 = std::chrono::steady_clock::now();
        rig.block(kBlock, 0, false);
        worstMs = std::max(
            worstMs,
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
    }
    // Blocking would mean waiting for the stalled read (seconds); allow for CI scheduling noise.
    CHECK(worstMs < 50.0);
    CHECK(rig.player.underruns() > 0);
    CHECK(stopped.wait_for(std::chrono::milliseconds(0)) == std::future_status::timeout);

    reader->stalled = false;
    CHECK(stopped.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    CHECK_FALSE(rig.streamer.running());
}

TEST_CASE("DiskStreamer: the background thread keeps a playing file fed") {
    PlayerRig rig(ramp(48000 * 5));
    rig.streamer.start();
    rig.player.play();
    // 3 s of audio (3 × the ring) in real time-ish: 128 samples every ~2.7 ms.
    std::vector<float> all;
    for (int i = 0; i < 1125; ++i) {
        auto b = rig.block(kBlock, 0, false);
        all.insert(all.end(), b.begin(), b.end());
        std::this_thread::sleep_for(std::chrono::microseconds(2000));
    }
    rig.streamer.stop();
    CHECK(rig.player.underruns() == 0);
    for (std::size_t i = 0; i < all.size(); ++i)
        REQUIRE(RampReader::frameOf(all[i]) == static_cast<std::int64_t>(i));
}

TEST_CASE("DiskStreamer: streams are held weakly") {
    DiskStreamer streamer;
    {
        FilePlayer p({}, 1, ramp(1000), &streamer, "x");
        p.prepare(kSr, kBlock);
        CHECK(streamer.numStreams() == 1);
    }
    CHECK(streamer.numStreams() == 0);
    streamer.serviceAll();
}
