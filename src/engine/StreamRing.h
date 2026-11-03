#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <vector>

namespace spacer {

/// Lock-free single-producer / single-consumer multi-channel sample ring for
/// disk streaming (SPEC §5.1). The producer is the disk thread, the consumer
/// the audio thread. Positions are 64-bit running frame counts (they never
/// wrap), so "the frame written at count N" names a point in the stream that
/// both sides agree on — used to mark seek boundaries and the end of a file.
///
/// allocate() is non-RT and must happen before either side runs. Everything
/// else is RT-safe (no allocation, no locks).
class StreamRing {
public:
    void allocate(int numChannels, int capacityFrames) {
        numChannels_ = std::max(1, numChannels);
        capacity_ = std::max(1, capacityFrames);
        data_.assign(static_cast<std::size_t>(numChannels_) * static_cast<std::size_t>(capacity_), 0.0f);
        write_.store(0, std::memory_order_relaxed);
        read_.store(0, std::memory_order_relaxed);
    }

    int numChannels() const noexcept { return numChannels_; }
    int capacity() const noexcept { return capacity_; }

    // ---- producer ----
    std::int64_t writeCount() const noexcept { return write_.load(std::memory_order_acquire); }
    int freeSpace() const noexcept {
        const auto used = write_.load(std::memory_order_relaxed) - read_.load(std::memory_order_acquire);
        return capacity_ - static_cast<int>(used);
    }
    /// Appends `n` frames (n <= freeSpace()) from `src[channel]`.
    void write(const float* const* src, int n) noexcept {
        const std::int64_t w = write_.load(std::memory_order_relaxed);
        for (int c = 0; c < numChannels_; ++c) copyIn(c, w, src[c], n);
        write_.store(w + n, std::memory_order_release);
    }

    // ---- consumer ----
    std::int64_t readCount() const noexcept { return read_.load(std::memory_order_relaxed); }
    int available() const noexcept {
        return static_cast<int>(write_.load(std::memory_order_acquire) -
                                read_.load(std::memory_order_relaxed));
    }
    /// Copies `n` frames (n <= available()) into `dest[0..numDest)`. Channels
    /// beyond the ring's are given the ring's last channel.
    void read(float* const* dest, int numDest, int n) noexcept {
        const std::int64_t r = read_.load(std::memory_order_relaxed);
        for (int c = 0; c < numDest; ++c) copyOut(std::min(c, numChannels_ - 1), r, dest[c], n);
        read_.store(r + n, std::memory_order_release);
    }
    /// Discards everything up to running count `count` (clamped to what has
    /// been written; never moves backwards).
    void skipTo(std::int64_t count) noexcept {
        const std::int64_t w = write_.load(std::memory_order_acquire);
        const std::int64_t r = read_.load(std::memory_order_relaxed);
        read_.store(std::clamp(count, r, w), std::memory_order_release);
    }

private:
    float* channel(int c) noexcept {
        return data_.data() + static_cast<std::size_t>(c) * static_cast<std::size_t>(capacity_);
    }

    void copyIn(int c, std::int64_t pos, const float* src, int n) noexcept {
        const int start = static_cast<int>(pos % capacity_);
        const int first = std::min(n, capacity_ - start);
        float* ch = channel(c);
        std::copy(src, src + first, ch + start);
        std::copy(src + first, src + n, ch);
    }
    void copyOut(int c, std::int64_t pos, float* dst, int n) noexcept {
        const int start = static_cast<int>(pos % capacity_);
        const int first = std::min(n, capacity_ - start);
        const float* ch = channel(c);
        std::copy(ch + start, ch + start + first, dst);
        std::copy(ch, ch + (n - first), dst + first);
    }

    int numChannels_{1};
    int capacity_{1};
    std::vector<float> data_;
    std::atomic<std::int64_t> write_{0};
    std::atomic<std::int64_t> read_{0};
};

}  // namespace spacer
