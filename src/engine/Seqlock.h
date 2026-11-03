#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace spacer {

/// Single-writer / multi-reader sequence lock for a small trivially copyable
/// value (e.g. the audio thread publishing its clock state to the message
/// thread). The writer never waits and never allocates, so store() is
/// real-time safe; a reader retries while a store is in progress and always
/// returns a value that was stored as a whole (never a torn mix).
///
/// The payload lives in relaxed atomic words, so concurrent access is free of
/// data races by the C++ memory model (no plain-memory seqlock UB).
template <typename T>
class Seqlock {
    static_assert(std::is_trivially_copyable_v<T>);
    static constexpr std::size_t kWords = (sizeof(T) + sizeof(std::uint64_t) - 1) / sizeof(std::uint64_t);

public:
    Seqlock() { store(T{}); }
    explicit Seqlock(const T& value) { store(value); }

    /// Writer (one thread only).
    void store(const T& value) noexcept {
        std::array<std::uint64_t, kWords> words{};
        std::memcpy(words.data(), static_cast<const void*>(&value), sizeof(T));
        const std::uint32_t s = seq_.load(std::memory_order_relaxed);
        seq_.store(s + 1, std::memory_order_relaxed);  // odd: write in progress
        std::atomic_thread_fence(std::memory_order_release);
        for (std::size_t i = 0; i < kWords; ++i) words_[i].store(words[i], std::memory_order_relaxed);
        seq_.store(s + 2, std::memory_order_release);
    }

    /// Any thread.
    T load() const noexcept {
        std::array<std::uint64_t, kWords> words{};
        for (;;) {
            const std::uint32_t before = seq_.load(std::memory_order_acquire);
            if ((before & 1u) != 0) continue;
            for (std::size_t i = 0; i < kWords; ++i) words[i] = words_[i].load(std::memory_order_relaxed);
            std::atomic_thread_fence(std::memory_order_acquire);
            if (seq_.load(std::memory_order_relaxed) == before) break;
        }
        T out;
        std::memcpy(static_cast<void*>(&out), words.data(), sizeof(T));
        return out;
    }

private:
    std::atomic<std::uint32_t> seq_{0};
    std::array<std::atomic<std::uint64_t>, kWords> words_{};
};

}  // namespace spacer
