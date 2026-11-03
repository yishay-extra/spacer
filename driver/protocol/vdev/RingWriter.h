#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>

#include "vdev/Protocol.h"

namespace spacer::vdev {

/// Producer side of the ring (the driver's IO thread). Real-time safe: no
/// allocation, no locks, no system calls.
class RingWriter {
public:
    RingWriter() = default;
    /// `region` is a mapped region already set up with initialiseHeader().
    explicit RingWriter(void* region) noexcept : header_(headerOf(region)), ring_(ringOf(region)) {}

    bool valid() const noexcept { return header_ != nullptr; }
    Header* header() const noexcept { return header_; }

    /// Free space in frames.
    std::uint32_t space() const noexcept {
        const std::uint64_t w = header_->writeIndex.load(std::memory_order_relaxed);
        const std::uint64_t r = header_->readIndex.load(std::memory_order_acquire);
        const std::uint64_t used = w - r;
        return used >= kRingFrames ? 0u : static_cast<std::uint32_t>(kRingFrames - used);
    }

    /// Appends `frames` frames of kNumChannels interleaved samples. Frames that
    /// do not fit (the app is not reading) are dropped and counted as overruns.
    /// `interleaved` may be null: silence is written. `sampleTime` is the
    /// driver's sample time of the first frame, `hostTimeNs` the time of the
    /// write on the protocol clock (monotonicNowNs()). Returns the number of
    /// frames written.
    std::uint32_t write(const float* interleaved, std::uint32_t frames, std::uint64_t sampleTime = 0,
                        std::uint64_t hostTimeNs = 0) noexcept {
        if (header_ == nullptr || frames == 0) return 0;
        const std::uint32_t n = std::min(frames, space());
        const std::uint64_t w = header_->writeIndex.load(std::memory_order_relaxed);
        std::uint32_t done = 0;
        while (done < n) {
            const std::uint32_t slot = static_cast<std::uint32_t>((w + done) & (kRingFrames - 1));
            const std::uint32_t chunk = std::min(n - done, kRingFrames - slot);
            float* dst = ring_ + std::size_t{slot} * kNumChannels;
            const std::size_t samples = std::size_t{chunk} * kNumChannels;
            if (interleaved != nullptr)
                std::memcpy(dst, interleaved + std::size_t{done} * kNumChannels, samples * sizeof(float));
            else
                std::memset(dst, 0, samples * sizeof(float));
            done += chunk;
        }
        header_->lastWriteFrames.store(frames, std::memory_order_relaxed);
        header_->lastWriteSampleTime.store(sampleTime, std::memory_order_relaxed);
        header_->lastWriteHostTimeNs.store(hostTimeNs, std::memory_order_relaxed);
        if (n < frames) header_->overruns.fetch_add(frames - n, std::memory_order_relaxed);
        header_->writeIndex.store(w + n, std::memory_order_release);
        return n;
    }

private:
    Header* header_{nullptr};
    float* ring_{nullptr};
};

}  // namespace spacer::vdev
