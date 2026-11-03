#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>

#include "vdev/Protocol.h"

namespace spacer::vdev {

/// Consumer side of the ring (the app's audio thread). Real-time safe: no
/// allocation, no locks, no system calls.
class RingReader {
public:
    RingReader() = default;
    /// `region` must have passed checkHeader().
    explicit RingReader(void* region) noexcept : header_(headerOf(region)), ring_(ringOf(region)) {}

    bool valid() const noexcept { return header_ != nullptr; }
    Header* header() const noexcept { return header_; }

    /// Frames written and not yet consumed.
    std::uint32_t available() const noexcept {
        const std::uint64_t w = header_->writeIndex.load(std::memory_order_acquire);
        const std::uint64_t r = header_->readIndex.load(std::memory_order_relaxed);
        const std::uint64_t used = w - r;
        // A producer that restarted its counter (should not happen within a
        // session) reads as empty rather than as a huge backlog.
        return used > kRingFrames ? 0u : static_cast<std::uint32_t>(used);
    }

    /// Copies `frames` (≤ available()) frames, without consuming them, into
    /// per-channel buffers: `dest[c][i]` = channel c of frame i, for
    /// c < numDest (≤ kNumChannels); null entries are skipped.
    void peek(std::uint32_t frames, float* const* dest, std::uint32_t numDest) const noexcept {
        numDest = std::min(numDest, kNumChannels);
        const std::uint64_t r = header_->readIndex.load(std::memory_order_relaxed);
        std::uint32_t done = 0;
        while (done < frames) {
            const std::uint32_t slot = static_cast<std::uint32_t>((r + done) & (kRingFrames - 1));
            const std::uint32_t chunk = std::min(frames - done, kRingFrames - slot);
            const float* src = ring_ + std::size_t{slot} * kNumChannels;
            for (std::uint32_t c = 0; c < numDest; ++c) {
                float* out = dest[c];
                if (out == nullptr) continue;
                out += done;
                for (std::uint32_t i = 0; i < chunk; ++i) out[i] = src[std::size_t{i} * kNumChannels + c];
            }
            done += chunk;
        }
    }

    /// Marks `frames` (≤ available()) frames as read.
    void consume(std::uint32_t frames) noexcept {
        const std::uint64_t r = header_->readIndex.load(std::memory_order_relaxed);
        header_->readIndex.store(r + frames, std::memory_order_release);
    }

    /// Skips ahead so that at most `keep` unread frames remain (drops the
    /// oldest). Returns the number of frames skipped.
    std::uint32_t skipToLatest(std::uint32_t keep) noexcept {
        const std::uint32_t avail = available();
        if (avail <= keep) return 0;
        consume(avail - keep);
        return avail - keep;
    }

    void addUnderrun(std::uint32_t frames) noexcept {
        header_->underruns.fetch_add(frames, std::memory_order_relaxed);
    }

private:
    Header* header_{nullptr};
    float* ring_{nullptr};
};

}  // namespace spacer::vdev
