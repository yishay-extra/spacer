#include "engine/AllocationGuard.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <new>

namespace spacer::allocguard {

namespace {
thread_local bool t_inAudioThread = false;
std::atomic<Mode> g_mode{Mode::Assert};
std::atomic<std::uint64_t> g_violations{0};
}  // namespace

void setMode(Mode mode) noexcept {
    g_mode.store(mode, std::memory_order_relaxed);
}
std::uint64_t violations() noexcept {
    return g_violations.load(std::memory_order_relaxed);
}
void resetViolations() noexcept {
    g_violations.store(0, std::memory_order_relaxed);
}

bool enabled() noexcept {
#if SPACER_ALLOCATION_GUARD
    return true;
#else
    return false;
#endif
}

ScopedAudioThread::ScopedAudioThread() noexcept : previous_(t_inAudioThread) {
    t_inAudioThread = true;
}
ScopedAudioThread::~ScopedAudioThread() {
    t_inAudioThread = previous_;
}

#if SPACER_ALLOCATION_GUARD
namespace detail {
void onHeapUse(const char* what) noexcept;
void onHeapUse(const char* what) noexcept {
    if (!t_inAudioThread) return;
    g_violations.fetch_add(1, std::memory_order_relaxed);
    if (g_mode.load(std::memory_order_relaxed) == Mode::Assert) {
        t_inAudioThread = false;  // let the report itself allocate
        std::fprintf(stderr, "spacer: %s on the audio thread (allocation guard)\n", what);
        std::abort();
    }
}
}  // namespace detail
#endif

}  // namespace spacer::allocguard

#if SPACER_ALLOCATION_GUARD

void* operator new(std::size_t n) {
    spacer::allocguard::detail::onHeapUse("operator new");
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) {
    spacer::allocguard::detail::onHeapUse("operator new[]");
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new(std::size_t n, const std::nothrow_t&) noexcept {
    spacer::allocguard::detail::onHeapUse("operator new");
    return std::malloc(n ? n : 1);
}
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept {
    spacer::allocguard::detail::onHeapUse("operator new[]");
    return std::malloc(n ? n : 1);
}
void operator delete(void* p) noexcept {
    if (p) spacer::allocguard::detail::onHeapUse("operator delete");
    std::free(p);
}
void operator delete[](void* p) noexcept {
    if (p) spacer::allocguard::detail::onHeapUse("operator delete[]");
    std::free(p);
}
#if defined(__cpp_sized_deallocation)
void operator delete(void* p, std::size_t) noexcept {
    if (p) spacer::allocguard::detail::onHeapUse("operator delete");
    std::free(p);
}
void operator delete[](void* p, std::size_t) noexcept {
    if (p) spacer::allocguard::detail::onHeapUse("operator delete[]");
    std::free(p);
}
#endif

#endif
