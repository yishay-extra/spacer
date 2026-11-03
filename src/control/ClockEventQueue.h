#pragma once

#include <atomic>
#include <cstdint>

#include "engine/SpscQueue.h"

namespace spacer {

/// Hand-over of timestamped clock input (MIDI clock / MTC bytes, OSC beats)
/// from control threads to the audio thread, where the active external clock
/// consumes it (SPEC §5.2: data fed from other threads reaches the audio
/// thread lock-free; these events never go through the command queue).
///
/// Consumer side (pop) is lock-free and real-time safe. Producers may be
/// several threads (e.g. one per MIDI input): they serialise among
/// themselves with a spin flag that the audio thread never touches. Items are
/// only queued while a consumer is attached (an external clock of that kind
/// exists), so an idle queue never fills with stale events.
template <typename T, int Capacity>
class ClockEventQueue {
public:
    /// Any non-audio thread. False if no consumer is attached or the queue is full.
    bool push(const T& item) noexcept {
        if (consumers_.load(std::memory_order_acquire) <= 0) return false;
        while (producerLock_.test_and_set(std::memory_order_acquire)) {
        }
        const bool ok = queue_.push(item);
        producerLock_.clear(std::memory_order_release);
        return ok;
    }

    /// Audio thread (the attached consumer).
    bool pop(T& out) noexcept { return queue_.pop(out); }

    /// Consumer lifetime (any thread; the clock's constructor / destructor).
    void attach() noexcept { consumers_.fetch_add(1, std::memory_order_acq_rel); }
    void detach() noexcept { consumers_.fetch_sub(1, std::memory_order_acq_rel); }
    bool hasConsumer() const noexcept { return consumers_.load(std::memory_order_acquire) > 0; }

    std::uint64_t dropped() const noexcept { return queue_.dropped(); }

private:
    SpscQueue<T, Capacity> queue_;
    std::atomic_flag producerLock_ = ATOMIC_FLAG_INIT;
    std::atomic<int> consumers_{0};
};

/// A short MIDI message with its host timestamp (seconds). Long SysEx other
/// than MTC full-frame (10 bytes) is never queued.
struct ClockMidiEvent {
    double hostTime{0.0};
    std::uint8_t size{0};
    std::uint8_t data[10]{};
};

/// An OSC transport event for OscClock.
struct ClockOscEvent {
    enum class Kind : std::uint8_t { Beat, Stop };
    double hostTime{0.0};
    double beat{0.0};
    Kind kind{Kind::Beat};
};

}  // namespace spacer
