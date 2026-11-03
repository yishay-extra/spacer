// Spacer.driver <-> app shared-memory ring (driver/protocol/vdev, SPEC §14).

#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "vdev/Protocol.h"
#include "vdev/RingReader.h"
#include "vdev/RingWriter.h"
#include "vdev/SharedMemory.h"

using namespace spacer::vdev;

namespace {

/// A zero-filled, page-aligned in-process region (no shm needed).
struct LocalRegion {
    std::vector<std::uint64_t> storage =
        std::vector<std::uint64_t>(kRegionBytes / sizeof(std::uint64_t) + 1, 0);
    void* data() { return storage.data(); }
};

std::string uniqueName(const char* tag) {
    return std::string("/spacer.t.") + tag + "." + std::to_string(::getpid());
}

/// The value the tests put in channel c of frame index i.
float pattern(std::uint64_t i, std::uint32_t c) {
    return static_cast<float>((i % 100000) * 64 + c);
}

std::vector<float> patternFrames(std::uint64_t first, std::uint32_t frames) {
    std::vector<float> v(std::size_t{frames} * kNumChannels);
    for (std::uint32_t f = 0; f < frames; ++f)
        for (std::uint32_t c = 0; c < kNumChannels; ++c)
            v[std::size_t{f} * kNumChannels + c] = pattern(first + f, c);
    return v;
}

struct ChannelBuffers {
    explicit ChannelBuffers(std::uint32_t frames)
        : data(std::size_t{kNumChannels} * frames), ptrs(kNumChannels) {
        for (std::uint32_t c = 0; c < kNumChannels; ++c) ptrs[c] = data.data() + std::size_t{c} * frames;
    }
    std::vector<float> data;
    std::vector<float*> ptrs;
};

}  // namespace

TEST_CASE("vdev: layout constants follow SPEC §14 and the region is page-aligned") {
    CHECK(kNumChannels == 64);
    CHECK(kRingFrames >= 8192);
    CHECK(sizeof(Header) <= kDataOffset);
    CHECK(kDataOffset % 64 == 0);
    CHECK(kRegionBytes == kDataOffset + std::size_t{kRingFrames} * 64 * sizeof(float));
    CHECK(isSupportedSampleRate(44100.0));
    CHECK(isSupportedSampleRate(96000.0));
    CHECK_FALSE(isSupportedSampleRate(22050.0));
    // magic and protocolVersion stay at fixed offsets in every protocol version.
    CHECK(offsetof(Header, protocolVersion) == 4);
}

TEST_CASE("vdev: checkHeader accepts an initialised region and rejects others") {
    LocalRegion r;
    CHECK(checkHeader(r.data(), kRegionBytes) == HeaderCheck::NotInitialised);
    initialiseHeader(r.data(), 42, "1.2.3", 48000.0);
    CHECK(checkHeader(r.data(), kRegionBytes) == HeaderCheck::Ok);
    CHECK(std::string(headerOf(r.data())->driverVersion) == "1.2.3");
    CHECK(headerOf(r.data())->sessionId == 42);
    CHECK(headerOf(r.data())->sampleRate.load() == 48000.0);
    CHECK(checkHeader(r.data(), 16) == HeaderCheck::TooSmall);
    CHECK(checkHeader(r.data(), kRegionBytes - 1) == HeaderCheck::BadGeometry);

    headerOf(r.data())->protocolVersion = kProtocolVersion + 1;
    CHECK(checkHeader(r.data(), kRegionBytes) == HeaderCheck::VersionMismatch);
    headerOf(r.data())->protocolVersion = kProtocolVersion;
    headerOf(r.data())->numChannels = 32;
    CHECK(checkHeader(r.data(), kRegionBytes) == HeaderCheck::BadGeometry);
}

TEST_CASE("vdev: driver version strings are truncated and NUL-terminated") {
    LocalRegion r;
    const std::string longVersion(100, 'x');
    initialiseHeader(r.data(), 1, longVersion.c_str(), 48000.0);
    CHECK(std::string(headerOf(r.data())->driverVersion) == std::string(31, 'x'));
}

TEST_CASE("vdev: frames written are read back per channel, across the wrap point") {
    LocalRegion r;
    initialiseHeader(r.data(), 1, "t", 48000.0);
    RingWriter w(r.data());
    RingReader rd(r.data());
    ChannelBuffers out(1000);

    std::uint64_t written = 0, read = 0;
    // Enough rounds to wrap the ring several times with an odd chunk size.
    for (int round = 0; round < 60; ++round) {
        const auto frames = patternFrames(written, 997);
        REQUIRE(w.write(frames.data(), 997) == 997);
        written += 997;
        REQUIRE(rd.available() == 997);
        rd.peek(997, out.ptrs.data(), kNumChannels);
        for (std::uint32_t c : {0u, 6u, 63u})
            for (std::uint32_t i : {0u, 500u, 996u}) REQUIRE(out.ptrs[c][i] == pattern(read + i, c));
        rd.consume(997);
        read += 997;
    }
    CHECK(read > 3 * kRingFrames);
    CHECK(headerOf(r.data())->overruns.load() == 0);
    CHECK(headerOf(r.data())->lastWriteFrames.load() == 997);
}

TEST_CASE("vdev: peek does not consume; null destinations are skipped") {
    LocalRegion r;
    initialiseHeader(r.data(), 1, "t", 48000.0);
    RingWriter w(r.data());
    RingReader rd(r.data());
    const auto frames = patternFrames(0, 10);
    w.write(frames.data(), 10);
    ChannelBuffers out(10);
    out.ptrs[3] = nullptr;
    rd.peek(10, out.ptrs.data(), kNumChannels);
    rd.peek(10, out.ptrs.data(), kNumChannels);
    CHECK(rd.available() == 10);
    CHECK(out.ptrs[7][9] == pattern(9, 7));
}

TEST_CASE("vdev: a full ring drops the newest frames and counts overruns") {
    LocalRegion r;
    initialiseHeader(r.data(), 1, "t", 48000.0);
    RingWriter w(r.data());
    RingReader rd(r.data());
    const auto a = patternFrames(0, kRingFrames - 100);
    CHECK(w.write(a.data(), kRingFrames - 100) == kRingFrames - 100);
    const auto b = patternFrames(kRingFrames - 100, 300);
    CHECK(w.write(b.data(), 300) == 100);
    CHECK(headerOf(r.data())->overruns.load() == 200);
    CHECK(w.space() == 0);
    CHECK(w.write(b.data(), 10) == 0);
    CHECK(headerOf(r.data())->overruns.load() == 210);

    // Oldest frames are intact.
    ChannelBuffers out(4);
    rd.peek(4, out.ptrs.data(), kNumChannels);
    CHECK(out.ptrs[5][0] == pattern(0, 5));
    rd.consume(4);
    CHECK(w.space() == 4);
}

TEST_CASE("vdev: null input writes silence") {
    LocalRegion r;
    initialiseHeader(r.data(), 1, "t", 48000.0);
    RingWriter w(r.data());
    RingReader rd(r.data());
    const auto a = patternFrames(0, 8);
    w.write(a.data(), 8);
    rd.consume(8);
    w.write(nullptr, 8);
    ChannelBuffers out(8);
    rd.peek(8, out.ptrs.data(), kNumChannels);
    for (std::uint32_t c = 0; c < kNumChannels; ++c) CHECK(out.ptrs[c][7] == 0.0f);
}

TEST_CASE("vdev: skipToLatest keeps the newest frames") {
    LocalRegion r;
    initialiseHeader(r.data(), 1, "t", 48000.0);
    RingWriter w(r.data());
    RingReader rd(r.data());
    const auto a = patternFrames(0, 1000);
    w.write(a.data(), 1000);
    CHECK(rd.skipToLatest(2000) == 0);
    CHECK(rd.skipToLatest(100) == 900);
    CHECK(rd.available() == 100);
    ChannelBuffers out(1);
    rd.peek(1, out.ptrs.data(), kNumChannels);
    CHECK(out.ptrs[2][0] == pattern(900, 2));
}

TEST_CASE("vdev: producer and consumer threads see a gap-free, untorn stream") {
    LocalRegion r;
    initialiseHeader(r.data(), 1, "t", 48000.0);
    constexpr std::uint64_t total = 400000;
    std::atomic<bool> failed{false};

    std::thread producer([&] {
        RingWriter w(r.data());
        std::uint64_t next = 0;
        while (next < total) {
            const auto n = static_cast<std::uint32_t>(std::min<std::uint64_t>(1 + next % 509, total - next));
            if (w.space() < n) {
                std::this_thread::yield();
                continue;
            }
            const auto frames = patternFrames(next, n);
            w.write(frames.data(), n);
            next += n;
        }
    });

    RingReader rd(r.data());
    ChannelBuffers out(1024);
    std::uint64_t got = 0;
    while (got < total && !failed) {
        const std::uint32_t n =
            std::min<std::uint32_t>(rd.available(), 1 + static_cast<std::uint32_t>(got % 1021));
        if (n == 0) {
            std::this_thread::yield();
            continue;
        }
        rd.peek(n, out.ptrs.data(), kNumChannels);
        for (std::uint32_t i = 0; i < n; ++i)
            for (std::uint32_t c = 0; c < kNumChannels; c += 9)
                if (out.ptrs[c][i] != pattern(got + i, c)) failed = true;
        rd.consume(n);
        got += n;
    }
    producer.join();
    CHECK_FALSE(failed);
    CHECK(got == total);
    CHECK(headerOf(r.data())->overruns.load() == 0);
}

TEST_CASE("vdev: SharedMemory open fails cleanly when the object does not exist") {
    const auto name = uniqueName("missing");
    SharedMemory::unlink(name.c_str());
    std::string error;
    auto m = SharedMemory::open(name.c_str(), &error);
    CHECK_FALSE(m.valid());
    CHECK_FALSE(error.empty());
}

TEST_CASE("vdev: create replaces an existing object with a fresh, zeroed one") {
    const auto name = uniqueName("replace");
    {
        auto a = SharedMemory::create(name.c_str(), kRegionBytes);
        REQUIRE(a.valid());
        initialiseHeader(a.data(), 7, "old", 48000.0);
        auto b = SharedMemory::create(name.c_str(), kRegionBytes);
        REQUIRE(b.valid());
        CHECK(checkHeader(b.data(), b.size()) == HeaderCheck::NotInitialised);
        auto c = SharedMemory::open(name.c_str());
        REQUIRE(c.valid());
        CHECK(c.size() >= kRegionBytes);
        CHECK(checkHeader(c.data(), c.size()) == HeaderCheck::NotInitialised);  // b, not a
    }
    SharedMemory::unlink(name.c_str());
}

TEST_CASE("vdev: a second process reads what the first writes through POSIX shm") {
    const auto name = uniqueName("ipc");
    std::string error;
    auto region = SharedMemory::create(name.c_str(), kRegionBytes, &error);
    REQUIRE(region.valid());
    initialiseHeader(region.data(), 99, "ipc", 48000.0);

    constexpr std::uint64_t total = 200000;
    const pid_t child = ::fork();
    REQUIRE(child >= 0);
    if (child == 0) {
        // Consumer process (the app): open by name, read everything, verify.
        auto m = SharedMemory::open(name.c_str());
        if (!m.valid() || checkHeader(m.data(), m.size()) != HeaderCheck::Ok) ::_exit(2);
        if (headerOf(m.data())->sessionId != 99) ::_exit(3);
        RingReader rd(m.data());
        ChannelBuffers out(2048);
        std::uint64_t got = 0;
        int idle = 0;
        while (got < total) {
            const std::uint32_t n = std::min<std::uint32_t>(rd.available(), 2048);
            if (n == 0) {
                if (++idle > 50000000) ::_exit(4);  // producer vanished
                continue;
            }
            idle = 0;
            rd.peek(n, out.ptrs.data(), kNumChannels);
            for (std::uint32_t i = 0; i < n; ++i)
                if (out.ptrs[6][i] != pattern(got + i, 6) || out.ptrs[63][i] != pattern(got + i, 63))
                    ::_exit(5);
            rd.consume(n);
            got += n;
        }
        headerOf(m.data())->consumerHeartbeat.store(got);
        ::_exit(0);
    }

    // Producer process (the driver).
    RingWriter w(region.data());
    std::uint64_t next = 0;
    while (next < total) {
        const std::uint32_t n = static_cast<std::uint32_t>(std::min<std::uint64_t>(512, total - next));
        if (w.space() < n) {
            std::this_thread::yield();
            continue;
        }
        const auto frames = patternFrames(next, n);
        w.write(frames.data(), n);
        next += n;
    }
    int status = 0;
    REQUIRE(::waitpid(child, &status, 0) == child);
    REQUIRE(WIFEXITED(status));
    CHECK(WEXITSTATUS(status) == 0);
    CHECK(headerOf(region.data())->consumerHeartbeat.load() == total);
    CHECK(headerOf(region.data())->overruns.load() == 0);
    SharedMemory::unlink(name.c_str());
}
