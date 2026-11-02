#pragma once

#include <cstdint>

namespace spacer::allocguard {

/// Real-time allocation guard (SPEC §17). When the build defines
/// SPACER_ALLOCATION_GUARD=1 (debug app builds and the engine tests), the
/// global operator new / delete are replaced and check whether the calling
/// thread is inside a ScopedAudioThread. What happens then depends on the
/// mode: Assert (default) aborts with a message, Count only counts.
/// Without the define the scope is a no-op and nothing is replaced.
enum class Mode { Assert, Count };

void setMode(Mode mode) noexcept;

/// Allocations + frees seen inside a ScopedAudioThread since the last reset.
std::uint64_t violations() noexcept;
void resetViolations() noexcept;

/// True when the replacement operators are compiled in.
bool enabled() noexcept;

/// Marks the current thread as being inside the audio callback.
class ScopedAudioThread {
public:
    ScopedAudioThread() noexcept;
    ~ScopedAudioThread();
    ScopedAudioThread(const ScopedAudioThread&) = delete;
    ScopedAudioThread& operator=(const ScopedAudioThread&) = delete;

private:
    bool previous_;
};

}  // namespace spacer::allocguard
