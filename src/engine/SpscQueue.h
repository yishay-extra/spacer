#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <type_traits>

namespace spacer {

/// Fixed-capacity, lock-free single-producer / single-consumer queue built on
/// juce::AbstractFifo. Storage is inline (no allocation after construction),
/// so push() and pop() are real-time safe on both ends. Items that do not fit
/// are dropped and counted (SPEC §5.2).
///
/// Exactly one thread may call push(), and exactly one (other) thread may call
/// pop(). Construct on the heap: the storage is `Capacity * sizeof(T)` bytes.
template <typename T, int Capacity>
class SpscQueue {
    static_assert(std::is_trivially_copyable_v<T>, "queue items must be trivially copyable");
    static_assert(Capacity > 1);

public:
    /// Producer side. Returns false (and counts a drop) if the queue is full.
    bool push(const T& item) noexcept {
        const auto scope = fifo_.write(1);
        if (scope.blockSize1 + scope.blockSize2 != 1) {
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        items_[static_cast<std::size_t>(scope.blockSize1 == 1 ? scope.startIndex1 : scope.startIndex2)] =
            item;
        return true;
    }

    /// Consumer side. Returns false if the queue is empty.
    bool pop(T& out) noexcept {
        const auto scope = fifo_.read(1);
        if (scope.blockSize1 + scope.blockSize2 != 1) return false;
        out = items_[static_cast<std::size_t>(scope.blockSize1 == 1 ? scope.startIndex1 : scope.startIndex2)];
        return true;
    }

    /// Either side; approximate while the other side is active.
    int size() const noexcept { return fifo_.getNumReady(); }
    static constexpr int capacity() noexcept { return Capacity; }

    /// Number of items dropped because the queue was full, since construction.
    std::uint64_t dropped() const noexcept { return dropped_.load(std::memory_order_relaxed); }

private:
    // AbstractFifo keeps one slot free to tell full from empty.
    juce::AbstractFifo fifo_{Capacity + 1};
    std::array<T, static_cast<std::size_t>(Capacity) + 1> items_{};
    std::atomic<std::uint64_t> dropped_{0};
};

}  // namespace spacer
