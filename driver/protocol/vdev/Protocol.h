#pragma once

// Spacer virtual-device shared-memory protocol (SPEC §14).
//
// Shared between Spacer.driver (the CoreAudio HAL plug-in, producer) and the
// Spacer app's engine (VirtualDeviceReceiver, consumer). Plain C++20 and
// POSIX only: no JUCE, no spacer_core, no CoreAudio, so it builds and is
// tested on Linux as well as macOS.
//
// Memory layout of the region (`kSharedMemoryName`, created by the driver):
//
//   [0, kDataOffset)          Header (below), padded to one page
//   [kDataOffset, end)        float32 ring: kRingFrames frames of
//                             kNumChannels interleaved samples each
//
// The ring is single-producer / single-consumer and lock-free:
//   - the driver owns `writeIndex` (total frames ever written — the sequence
//     counter) and only ever reads `readIndex`;
//   - the app owns `readIndex` (total frames consumed) and only ever reads
//     `writeIndex`.
// Frame `i` lives at ring slot `i & (kRingFrames - 1)`. The producer never
// overwrites unread frames: if the ring is full the newest frames are dropped
// and counted in `overruns`. The consumer may skip ahead (resync) by moving
// `readIndex` forward, never backwards.
//
// Versioning: `magic` + `protocolVersion` + geometry are checked by the
// consumer before it touches anything else. A change to this layout must bump
// kProtocolVersion; the app refuses (and reports) a driver whose version it
// does not speak.

#include <time.h>

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace spacer::vdev {

/// POSIX shm name. macOS limits names to 31 characters (PSHMNAMLEN).
inline constexpr char kSharedMemoryName[] = "/org.spacer.driver";
inline constexpr std::uint32_t kMagic = 0x52435053u;  // "SPCR" little-endian
inline constexpr std::uint32_t kProtocolVersion = 1;
/// SPEC §1: 64 channels, fixed in v1.
inline constexpr std::uint32_t kNumChannels = 64;
/// SPEC §14: ≥ 8192 frames. Power of two so slots are a mask.
inline constexpr std::uint32_t kRingFrames = 16384;
/// SPEC §14: default added latency, one driver buffer (user-selectable 128/256/512).
inline constexpr std::uint32_t kDefaultLatencyFrames = 512;
/// SPEC §14: 44.1–96 kHz.
inline constexpr double kSupportedSampleRates[] = {44100.0, 48000.0, 88200.0, 96000.0};
inline constexpr double kDefaultSampleRate = 48000.0;
/// The driver increments `heartbeat` at least this often while it is loaded.
inline constexpr int kHeartbeatIntervalMs = 250;
/// The app treats the driver as gone when the heartbeat has not moved for this long.
inline constexpr int kHeartbeatTimeoutMs = 2000;

static_assert((kRingFrames & (kRingFrames - 1)) == 0, "ring size must be a power of two");
static_assert(kRingFrames >= 8192, "SPEC §14: ring must hold at least 8192 frames");
static_assert(std::atomic<std::uint64_t>::is_always_lock_free, "shared atomics must be lock-free");
static_assert(std::atomic<std::uint32_t>::is_always_lock_free, "shared atomics must be lock-free");
static_assert(std::atomic<double>::is_always_lock_free, "shared atomics must be lock-free");

/// The protocol's host clock, in nanoseconds: the system monotonic clock
/// that both processes see (macOS CLOCK_UPTIME_RAW = mach_absolute_time;
/// CLOCK_MONOTONIC elsewhere). Real-time safe (no system call on macOS / Linux
/// vDSO).
inline std::uint64_t monotonicNowNs() noexcept {
    timespec ts{};
#if defined(__APPLE__)
    clock_gettime(CLOCK_UPTIME_RAW, &ts);
#else
    clock_gettime(CLOCK_MONOTONIC, &ts);
#endif
    return static_cast<std::uint64_t>(ts.tv_sec) * 1000000000ull + static_cast<std::uint64_t>(ts.tv_nsec);
}

/// Does the driver accept this nominal rate?
constexpr bool isSupportedSampleRate(double rate) noexcept {
    for (double r : kSupportedSampleRates)
        if (r - rate < 0.5 && rate - r < 0.5) return true;
    return false;
}

struct alignas(64) Header {
    // ---- written once by the driver when it creates the region -----------
    /// kMagic, stored last (release) so a reader never sees a half-built header.
    std::atomic<std::uint32_t> magic;
    std::uint32_t protocolVersion;
    std::uint32_t headerBytes;  // sizeof(Header)
    std::uint32_t numChannels;
    std::uint32_t ringFrames;
    std::uint32_t dataOffset;  // bytes from the start of the region to the ring
    /// Changes every time the driver (re)creates the region (coreaudiod restart).
    std::uint64_t sessionId;
    /// NUL-terminated, e.g. "0.1.0" (CFBundleShortVersionString).
    char driverVersion[32];

    // ---- producer (driver) ------------------------------------------------
    alignas(64) std::atomic<std::uint64_t> writeIndex;
    /// Nominal rate of the device as set in CoreAudio.
    std::atomic<double> sampleRate;
    /// 1 while at least one client is doing IO on the device.
    std::atomic<std::uint32_t> ioRunning;
    /// Size of the last IO write (the sender's buffer size), in frames.
    std::atomic<std::uint32_t> lastWriteFrames;
    /// Frames dropped because the ring was full (app not reading).
    std::atomic<std::uint64_t> overruns;
    /// Incremented every kHeartbeatIntervalMs while the driver is loaded.
    std::atomic<std::uint64_t> heartbeat;
    /// Clock info: the driver's sample time of the first frame of the last
    /// write, and the host time (monotonicNowNs()) at which it was written.
    /// The app uses the host time to interpolate the fill level between the
    /// sender's (chunk-sized) writes; 0 = unknown.
    std::atomic<std::uint64_t> lastWriteSampleTime;
    std::atomic<std::uint64_t> lastWriteHostTimeNs;

    // ---- consumer (app) ---------------------------------------------------
    alignas(64) std::atomic<std::uint64_t> readIndex;
    /// Frames the app had to replace with silence while the driver was running.
    std::atomic<std::uint64_t> underruns;
    /// Rate the app wants the device at (SPEC §14 "following the sample rate
    /// the Spacer app sets"); 0 = no request. The driver polls it.
    std::atomic<double> requestedSampleRate;
    /// Protocol version of the attached app (0 = none attached).
    std::atomic<std::uint32_t> consumerProtocolVersion;
    std::uint32_t consumerReserved;
    /// Incremented by the app while it is attached (diagnostic).
    std::atomic<std::uint64_t> consumerHeartbeat;
};

static_assert(sizeof(Header) <= 4096, "header must fit in the first page");

/// Byte offset of the ring (one page).
inline constexpr std::size_t kDataOffset = 4096;
inline constexpr std::size_t kRingSamples = std::size_t{kRingFrames} * kNumChannels;
inline constexpr std::size_t kRegionBytes = kDataOffset + kRingSamples * sizeof(float);

enum class HeaderCheck {
    Ok,
    TooSmall,         // region smaller than a header
    NotInitialised,   // magic not (yet) set
    VersionMismatch,  // a protocol this build does not speak
    BadGeometry,      // channels / ring size / offsets disagree with this build
};

inline const char* toString(HeaderCheck c) noexcept {
    switch (c) {
        case HeaderCheck::Ok:
            return "ok";
        case HeaderCheck::TooSmall:
            return "shared memory region too small";
        case HeaderCheck::NotInitialised:
            return "shared memory not initialised";
        case HeaderCheck::VersionMismatch:
            return "protocol version mismatch";
        case HeaderCheck::BadGeometry:
            return "unexpected channel count or ring size";
    }
    return "?";
}

/// Consumer-side validation of a mapped region of `regionBytes`.
inline HeaderCheck checkHeader(const void* region, std::size_t regionBytes) noexcept {
    if (region == nullptr || regionBytes < sizeof(Header)) return HeaderCheck::TooSmall;
    const auto* h = static_cast<const Header*>(region);
    if (h->magic.load(std::memory_order_acquire) != kMagic) return HeaderCheck::NotInitialised;
    if (h->protocolVersion != kProtocolVersion) return HeaderCheck::VersionMismatch;
    if (h->headerBytes != sizeof(Header) || h->numChannels != kNumChannels || h->ringFrames != kRingFrames ||
        h->dataOffset != kDataOffset || regionBytes < kRegionBytes)
        return HeaderCheck::BadGeometry;
    return HeaderCheck::Ok;
}

/// Producer-side initialisation of a fresh, zero-filled region of at least
/// kRegionBytes. Publishes the header last.
inline void initialiseHeader(void* region, std::uint64_t sessionId, const char* driverVersion,
                             double sampleRate) noexcept {
    auto* h = static_cast<Header*>(region);
    h->protocolVersion = kProtocolVersion;
    h->headerBytes = sizeof(Header);
    h->numChannels = kNumChannels;
    h->ringFrames = kRingFrames;
    h->dataOffset = static_cast<std::uint32_t>(kDataOffset);
    h->sessionId = sessionId;
    std::size_t i = 0;
    if (driverVersion != nullptr)
        for (; driverVersion[i] != '\0' && i + 1 < sizeof(h->driverVersion); ++i)
            h->driverVersion[i] = driverVersion[i];
    h->driverVersion[i] = '\0';
    h->writeIndex.store(0, std::memory_order_relaxed);
    h->sampleRate.store(sampleRate, std::memory_order_relaxed);
    h->ioRunning.store(0, std::memory_order_relaxed);
    h->lastWriteFrames.store(0, std::memory_order_relaxed);
    h->overruns.store(0, std::memory_order_relaxed);
    h->heartbeat.store(0, std::memory_order_relaxed);
    h->lastWriteSampleTime.store(0, std::memory_order_relaxed);
    h->lastWriteHostTimeNs.store(0, std::memory_order_relaxed);
    h->readIndex.store(0, std::memory_order_relaxed);
    h->underruns.store(0, std::memory_order_relaxed);
    h->requestedSampleRate.store(0.0, std::memory_order_relaxed);
    h->consumerProtocolVersion.store(0, std::memory_order_relaxed);
    h->consumerHeartbeat.store(0, std::memory_order_relaxed);
    h->magic.store(kMagic, std::memory_order_release);
}

inline Header* headerOf(void* region) noexcept {
    return static_cast<Header*>(region);
}
inline float* ringOf(void* region) noexcept {
    return reinterpret_cast<float*>(static_cast<unsigned char*>(region) + kDataOffset);
}

}  // namespace spacer::vdev
