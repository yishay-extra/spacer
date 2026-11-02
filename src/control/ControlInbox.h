#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace spacer {

/// Hand-over point from a control thread (OSC receiver, MIDI input callback)
/// to the message thread (SPEC §5.2). Many producers are fine, one consumer
/// drains it. This is NOT audio-thread code: it takes a short mutex and may
/// allocate. Items beyond `capacity` are dropped and counted.
template <typename T>
class ControlInbox {
public:
    explicit ControlInbox(std::size_t capacity = 4096) : capacity_(capacity) { items_.reserve(capacity); }

    bool push(T item) {
        const std::lock_guard<std::mutex> lock(mutex_);
        if (items_.size() >= capacity_) {
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        items_.push_back(std::move(item));
        return true;
    }

    /// Moves everything queued so far into `out` (appended), oldest first.
    void drainTo(std::vector<T>& out) {
        const std::lock_guard<std::mutex> lock(mutex_);
        for (auto& i : items_) out.push_back(std::move(i));
        items_.clear();
    }

    std::uint64_t dropped() const noexcept { return dropped_.load(std::memory_order_relaxed); }

private:
    std::mutex mutex_;
    std::vector<T> items_;
    std::size_t capacity_;
    std::atomic<std::uint64_t> dropped_{0};
};

}  // namespace spacer
