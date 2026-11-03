// VirtualDeviceReceiver + VirtualInput (SPEC §8, §14), against the real
// shared-memory protocol (driver/protocol) with a simulated driver.

#include <unistd.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <string>
#include <vector>

#include "EngineTestUtils.h"
#include "engine/AllocationGuard.h"
#include "engine/EngineProcessor.h"
#include "engine/GraphBuilder.h"
#include "engine/VirtualDeviceReceiver.h"
#include "engine/VirtualInput.h"
#include "vdev/Protocol.h"
#include "vdev/RingWriter.h"
#include "vdev/SharedMemory.h"

using namespace spacer;
using namespace spacer::testing;

namespace {

constexpr double kTwoPi = 2.0 * 3.14159265358979323846;

std::string uniqueName(const char* tag) {
    static int counter = 0;
    return std::string("/spacer.e.") + tag + "." + std::to_string(::getpid()) + "." +
           std::to_string(++counter);
}

/// Stands in for Spacer.driver: owns the region, writes the sender's audio
/// in chunks of `chunk` frames at `rate` (relative to the engine clock).
struct FakeDriver {
    explicit FakeDriver(std::string n, double sampleRate = 48000.0, std::uint64_t session = 1,
                        std::uint32_t protocol = vdev::kProtocolVersion)
        : name(std::move(n)) {
        memory = vdev::SharedMemory::create(name.c_str(), vdev::kRegionBytes);
        REQUIRE(memory.valid());
        vdev::initialiseHeader(memory.data(), session, "9.8.7", sampleRate);
        header()->protocolVersion = protocol;
        header()->ioRunning.store(1);
        writer = vdev::RingWriter(memory.data());
    }
    ~FakeDriver() { vdev::SharedMemory::unlink(name.c_str()); }

    vdev::Header* header() { return vdev::headerOf(memory.data()); }
    void beat() { header()->heartbeat.fetch_add(1); }

    /// Writes `frames` frames whose channel `channel` carries signal(n) for
    /// the stream's frame index n; other channels are silent.
    template <typename Signal>
    void write(std::uint32_t frames, int channel, Signal&& signal, std::uint64_t hostTimeNs = 0) {
        buffer.assign(std::size_t{frames} * vdev::kNumChannels, 0.0f);
        for (std::uint32_t f = 0; f < frames; ++f)
            buffer[std::size_t{f} * vdev::kNumChannels + static_cast<std::size_t>(channel)] =
                signal(static_cast<double>(written + f));
        writer.write(buffer.data(), frames, written, hostTimeNs);
        written += frames;
    }

    std::string name;
    vdev::SharedMemory memory;
    vdev::RingWriter writer;
    std::uint64_t written{0};
    std::vector<float> buffer;
};

/// Runs sender and engine against each other on a simulated timeline. The
/// engine pulls `block` frames per callback (engine clock, 48 kHz). The sender
/// produces at driverRate·(1+drift) and delivers buffers of `chunk` frames,
/// each written (and timestamped) when it is complete.
struct Simulation {
    Simulation(FakeDriver& d, VirtualDeviceReceiver& r, int blockSize, std::uint32_t chunkSize,
               double driftPpm, double rateRatio = 1.0)
        : driver(d),
          receiver(r),
          block(blockSize),
          chunk(chunkSize),
          chunkPeriod(chunkSize / (rateRatio * (1.0 + driftPpm * 1e-6))) {}

    static std::uint64_t ns(double frames) { return static_cast<std::uint64_t>(frames * 1e9 / 48000.0) + 1; }

    template <typename Signal>
    void step(int channel, Signal&& signal) {
        ++blocks;
        const double now = static_cast<double>(blocks * block);
        while (nextWrite <= now) {
            driver.write(chunk, channel, signal, ns(nextWrite));
            nextWrite += chunkPeriod;
        }
        receiver.pull(block, ns(now));
    }

    FakeDriver& driver;
    VirtualDeviceReceiver& receiver;
    int block;
    std::uint32_t chunk;
    double chunkPeriod;
    double nextWrite{0.0};
    long blocks{0};
};

}  // namespace

TEST_CASE("VirtualDevice: driver absent -> Absent with a message, silence") {
    VirtualDeviceReceiver r(uniqueName("absent"));
    r.prepare(48000.0, 256);
    r.poll(0.0);
    CHECK(r.state() == VirtualDeviceReceiver::State::Absent);
    CHECK_FALSE(r.status().message.empty());
    r.pull(256);
    for (int c = 0; c < VirtualDeviceReceiver::kNumChannels; ++c)
        CHECK(peakAbs(r.channels()[c], 256) == 0.0f);
}

TEST_CASE("VirtualDevice: connects, reports the driver version and requests the engine rate") {
    const auto name = uniqueName("connect");
    FakeDriver d(name);
    VirtualDeviceReceiver r(name);
    r.prepare(96000.0, 256);
    CHECK(r.poll(0.0));
    CHECK(r.state() == VirtualDeviceReceiver::State::Connected);
    const auto s = r.status();
    CHECK(s.driverVersion == "9.8.7");
    CHECK(s.protocolVersion == vdev::kProtocolVersion);
    CHECK(s.driverRunning);
    CHECK(d.header()->requestedSampleRate.load() == 96000.0);
    CHECK(d.header()->consumerProtocolVersion.load() == vdev::kProtocolVersion);
    // A later prepare (device change) updates the request.
    r.prepare(44100.0, 256);
    CHECK(d.header()->requestedSampleRate.load() == 44100.0);
}

TEST_CASE("VirtualDevice: an incompatible protocol is refused and reported") {
    const auto name = uniqueName("incompatible");
    FakeDriver d(name, 48000.0, 1, vdev::kProtocolVersion + 1);
    VirtualDeviceReceiver r(name);
    r.poll(0.0);
    CHECK(r.state() == VirtualDeviceReceiver::State::Incompatible);
    CHECK(r.status().message.find("protocol") != std::string::npos);
    CHECK(d.header()->consumerProtocolVersion.load() == 0);
}

TEST_CASE("VirtualDevice: a sine on channel 7 arrives on channel 7 only, within one driver buffer") {
    const auto name = uniqueName("ch7");
    FakeDriver d(name);
    VirtualDeviceReceiver r(name);
    constexpr int block = 128;
    r.prepare(48000.0, block);
    r.setLatencyFrames(512);
    r.poll(0.0);

    // Impulse at stream frame 20000 to measure the delay, sine elsewhere.
    constexpr double impulseAt = 20000.0;
    auto signal = [&](double n) {
        if (n == impulseAt) return 1.0f;
        if (n > impulseAt - 2000.0 && n < impulseAt + 2000.0) return 0.0f;
        return static_cast<float>(0.5 * std::sin(kTwoPi * 1000.0 * n / 48000.0));
    };
    Simulation sim(d, r, block, 512, 0.0);
    std::vector<float> ch7, other;
    long firstPlaying = -1;
    for (int i = 0; i < 400; ++i) {
        sim.step(7, signal);
        if (firstPlaying < 0 && r.status().playing) firstPlaying = static_cast<long>(ch7.size());
        ch7.insert(ch7.end(), r.channels()[7], r.channels()[7] + block);
        other.insert(other.end(), r.channels()[6], r.channels()[6] + block);
        other.insert(other.end(), r.channels()[8], r.channels()[8] + block);
    }
    REQUIRE(firstPlaying >= 0);
    CHECK(peakAbs(other.data(), static_cast<int>(other.size())) == 0.0f);
    // Steady-state sine at full level.
    CHECK(rms(ch7.data() + 40000, 4800) == Catch::Approx(0.5 / std::sqrt(2.0)).margin(0.01));

    // Engine time t carries sender frame t - delay; the sender writes frame n
    // at engine time ≈ n (same clock), so the delay is the added latency.
    int peakIndex = -1;
    float peak = 0.0f;
    for (int i = 15000; i < 25000; ++i)
        if (std::abs(ch7[static_cast<std::size_t>(i)]) > peak) {
            peak = std::abs(ch7[static_cast<std::size_t>(i)]);
            peakIndex = i;
        }
    REQUIRE(peakIndex > 0);
    const double delay = peakIndex - impulseAt;
    INFO("delay " << delay << " frames");
    UNSCOPED_INFO("measured virtual-device delay: " << delay << " frames");
    CHECK(delay >= 0.0);
    // One driver buffer (512) of latency, plus the sender's chunk granularity
    // inherent in any ring, plus the interpolator's two samples.
    CHECK(delay <= 512 + block + 4);
    CHECK(r.status().underrunFrames == 0);
}

TEST_CASE("VirtualDevice: clock drift is absorbed without underruns or growing latency") {
    struct Case {
        double ppm;
        int block;
        std::uint32_t chunk;
        int latency;
    };
    for (const Case k :
         {Case{-300.0, 256, 256, 256}, Case{0.0, 256, 256, 256}, Case{250.0, 256, 256, 256},
          Case{1000.0, 128, 512, 512}, Case{-1000.0, 512, 128, 128}, Case{80.0, 64, 1024, 128}}) {
        DYNAMIC_SECTION("drift " << k.ppm << " ppm, block " << k.block << ", sender buffer " << k.chunk) {
            const auto name = uniqueName("drift");
            FakeDriver d(name);
            VirtualDeviceReceiver r(name);
            r.prepare(48000.0, k.block);
            r.setLatencyFrames(k.latency);
            r.poll(0.0);
            Simulation sim(d, r, k.block, k.chunk, k.ppm);
            auto signal = [](double n) { return static_cast<float>(std::sin(kTwoPi * 440.0 * n / 48000.0)); };
            // 60 s of audio.
            const int blocks = 60 * 48000 / k.block;
            double minFill = 1e9, maxFill = 0.0;
            for (int i = 0; i < blocks; ++i) {
                sim.step(7, signal);
                if (i > blocks / 2) {
                    const double f = r.status().fillFrames;
                    minFill = std::min(minFill, f);
                    maxFill = std::max(maxFill, f);
                }
            }
            const auto s = r.status();
            INFO("ratio " << s.ratio << " fill " << minFill << ".." << maxFill);
            CHECK(s.playing);
            CHECK(s.underrunFrames == 0);
            CHECK(s.resyncs == 0);
            CHECK(s.ratio == Catch::Approx(1.0 + k.ppm * 1e-6).margin(30e-6));
            // The fill before a read stays within one sender buffer of target.
            const double target =
                k.block + std::max<double>(k.latency, k.chunk) + VirtualDeviceReceiver::kSafetyFrames;
            CHECK(minFill >= target - k.chunk - 16);
            CHECK(maxFill <= target + 16);
            CHECK(d.header()->overruns.load() == 0);
        }
    }
}

TEST_CASE("VirtualDevice: a driver at another nominal rate is resampled to the engine rate") {
    const auto name = uniqueName("rate");
    FakeDriver d(name, 44100.0);
    VirtualDeviceReceiver r(name);
    constexpr int block = 512;
    r.prepare(48000.0, block);
    r.poll(0.0);
    Simulation sim(d, r, block, 512, 0.0, 44100.0 / 48000.0);
    auto signal = [](double n) { return static_cast<float>(0.8 * std::sin(kTwoPi * 1000.0 * n / 44100.0)); };
    std::vector<float> out;
    for (int i = 0; i < 200; ++i) {
        sim.step(0, signal);
        out.insert(out.end(), r.channels()[0], r.channels()[0] + block);
    }
    CHECK(r.status().ratio == Catch::Approx(44100.0 / 48000.0).margin(1e-3));
    // 1 kHz at 48 kHz: count rising zero crossings over one second of steady output.
    const int start = 48000;
    int crossings = 0;
    for (int i = start + 1; i < start + 48000; ++i)
        if (out[static_cast<std::size_t>(i - 1)] < 0.0f && out[static_cast<std::size_t>(i)] >= 0.0f)
            ++crossings;
    CHECK(crossings >= 999);
    CHECK(crossings <= 1001);
    CHECK(rms(out.data() + start, 48000) == Catch::Approx(0.8 / std::sqrt(2.0)).margin(0.01));
}

TEST_CASE("VirtualDevice: underruns are counted while the driver runs, then playback re-primes") {
    const auto name = uniqueName("underrun");
    FakeDriver d(name);
    VirtualDeviceReceiver r(name);
    constexpr int block = 128;
    r.prepare(48000.0, block);
    r.setLatencyFrames(128);
    r.poll(0.0);
    auto one = [](double) { return 1.0f; };
    Simulation sim(d, r, block, 128, 0.0);
    for (int i = 0; i < 50; ++i) sim.step(0, one);
    REQUIRE(r.status().playing);
    CHECK(r.status().underrunFrames == 0);

    // The sender stalls while still "running": underrun, silence, counted.
    r.pull(block);
    r.pull(block);
    r.pull(block);
    CHECK_FALSE(r.status().playing);
    CHECK(r.status().underrunFrames > 0);
    CHECK(d.header()->underruns.load() == r.status().underrunFrames);
    CHECK(peakAbs(r.channels()[0], block) == 0.0f);

    // It resumes: the receiver primes again and plays.
    for (int i = 0; i < 20; ++i) sim.step(0, one);
    CHECK(r.status().playing);
    CHECK(r.channels()[0][block - 1] == Catch::Approx(1.0f));

    // When no client does IO, an empty ring is not an underrun.
    const auto before = r.status().underrunFrames;
    d.header()->ioRunning.store(0);
    for (int i = 0; i < 10; ++i) r.pull(block);
    CHECK(r.status().underrunFrames == before);
}

TEST_CASE("VirtualDevice: a stalled engine drops the backlog instead of lagging") {
    const auto name = uniqueName("stall");
    FakeDriver d(name);
    VirtualDeviceReceiver r(name);
    constexpr int block = 128;
    r.prepare(48000.0, block);
    r.poll(0.0);
    auto one = [](double) { return 1.0f; };
    Simulation sim(d, r, block, 128, 0.0);
    for (int i = 0; i < 50; ++i) sim.step(0, one);
    // The sender writes 12000 frames while the engine does not pull.
    for (int i = 0; i < 94; ++i) d.write(128, 0, one);
    r.pull(block);
    CHECK(r.status().resyncs == 1);
    CHECK(r.status().fillFrames > 10000);
    r.pull(block);
    CHECK(r.status().fillFrames < 1024);
}

TEST_CASE("VirtualDevice: heartbeat loss, driver restart and driver removal") {
    const auto name = uniqueName("restart");
    VirtualDeviceReceiver r(name);
    auto d = std::make_unique<FakeDriver>(name, 48000.0, 1);
    double t = 0.0;
    r.poll(t);
    REQUIRE(r.state() == VirtualDeviceReceiver::State::Connected);

    for (; t < 1.5; t += 0.1) {
        d->beat();
        r.poll(t);
    }
    CHECK(r.state() == VirtualDeviceReceiver::State::Connected);
    for (; t < 4.0; t += 0.1) r.poll(t);  // no heartbeat
    CHECK(r.state() == VirtualDeviceReceiver::State::NotResponding);
    d->beat();
    r.poll(t);
    CHECK(r.state() == VirtualDeviceReceiver::State::Connected);

    // coreaudiod restarts: the driver recreates the region (new session).
    for (; t < 7.0; t += 0.1) r.poll(t);
    REQUIRE(r.state() == VirtualDeviceReceiver::State::NotResponding);
    d.reset();
    d = std::make_unique<FakeDriver>(name, 48000.0, 2);
    for (int i = 0; i < 15; ++i, t += 0.1) {
        d->beat();
        r.poll(t);
    }
    CHECK(r.state() == VirtualDeviceReceiver::State::Connected);
    CHECK(d->header()->consumerProtocolVersion.load() == vdev::kProtocolVersion);
    CHECK(d->header()->requestedSampleRate.load() > 0.0);

    // The driver is removed.
    d.reset();
    for (int i = 0; i < 60; ++i, t += 0.1) r.poll(t);
    CHECK(r.state() == VirtualDeviceReceiver::State::Absent);
}

TEST_CASE("VirtualDevice: pull does not allocate") {
    const auto name = uniqueName("rt");
    FakeDriver d(name);
    VirtualDeviceReceiver r(name);
    r.prepare(48000.0, 256);
    r.poll(0.0);
    auto one = [](double) { return 0.25f; };
    std::vector<float> chunk(std::size_t{512} * vdev::kNumChannels, 0.25f);
    allocguard::setMode(allocguard::Mode::Count);
    allocguard::resetViolations();
    for (int i = 0; i < 400; ++i) {
        if (i % 2 == 0) d.writer.write(chunk.data(), 512);
        allocguard::ScopedAudioThread audio;
        r.pull(256);
    }
    CHECK(allocguard::violations() == 0);
    allocguard::setMode(allocguard::Mode::Assert);
    CHECK(r.status().playing);
    (void)one;
}

TEST_CASE("VirtualInput: picks its channels from the virtual inputs; silent without them") {
    std::vector<std::vector<float>> chans(64, std::vector<float>(16, 0.0f));
    std::vector<const float*> ptrs;
    for (int c = 0; c < 64; ++c) {
        std::fill(chans[static_cast<std::size_t>(c)].begin(), chans[static_cast<std::size_t>(c)].end(),
                  static_cast<float>(c));
        ptrs.push_back(chans[static_cast<std::size_t>(c)].data());
    }
    std::vector<float> l(16), rr(16);
    float* dest[2] = {l.data(), rr.data()};
    InputContext ctx;
    ctx.virtualInputs = ptrs.data();
    ctx.numVirtualInputs = 64;

    VirtualInput mono(ChannelInput{7, -1});
    mono.prepare(48000.0, 16);
    mono.read(ctx, dest, 1, 16);
    CHECK(l[15] == 7.0f);

    VirtualInput stereo(ChannelInput{10, 63});
    stereo.read(ctx, dest, 2, 16);
    CHECK(l[0] == 10.0f);
    CHECK(rr[0] == 63.0f);

    // Device inputs are not virtual inputs.
    InputContext hw;
    hw.deviceInputs = ptrs.data();
    hw.numDeviceInputs = 64;
    mono.read(hw, dest, 1, 16);
    CHECK(peakAbs(l.data(), 16) == 0.0f);
}

TEST_CASE("VirtualInput: end to end, driver channel 7 reaches the speaker through the engine") {
    const auto name = uniqueName("e2e");
    FakeDriver d(name);
    VirtualDeviceReceiver r(name);
    constexpr int block = 128;
    r.prepare(48000.0, block);
    r.poll(0.0);

    SourceSpec s = makeSource(1, {}, SpatializerKind::Direct);
    s.input.value = VirtualInputSpec{{7, -1}};
    s.directSpeakers = {1};
    Project p = makeProject({makeSpeaker(1, -30.0f, 2.0f, 0), makeSpeaker(2, 30.0f, 2.0f, 1)}, {s});
    EngineProcessor proc;
    proc.setGraphWhileStopped(GraphBuilder(p).build(48000.0, block));

    juce::AudioBuffer<float> out(2, block);
    float* outs[2] = {out.getWritePointer(0), out.getWritePointer(1)};
    auto signal = [](double n) { return static_cast<float>(0.5 * std::sin(kTwoPi * 500.0 * n / 48000.0)); };
    Simulation sim(d, r, block, 256, 0.0);
    std::vector<float> left, right;
    for (int i = 0; i < 200; ++i) {
        sim.step(7, signal);
        proc.process(nullptr, 0, outs, 2, block, r.channels(), VirtualDeviceReceiver::kNumChannels);
        left.insert(left.end(), outs[0], outs[0] + block);
        right.insert(right.end(), outs[1], outs[1] + block);
    }
    CHECK(rms(left.data() + 12800, 9600) == Catch::Approx(0.5 / std::sqrt(2.0)).margin(0.02));
    CHECK(peakAbs(right.data(), static_cast<int>(right.size())) == 0.0f);
}
