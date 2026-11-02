#include "AllocCounter.h"

#include <cstdlib>
#include <new>

namespace spacer::testing {
std::atomic<bool> g_countAllocs{false};
std::atomic<long> g_allocs{0};
}  // namespace spacer::testing

void* operator new(std::size_t n) {
    if (spacer::testing::g_countAllocs.load(std::memory_order_relaxed))
        spacer::testing::g_allocs.fetch_add(1, std::memory_order_relaxed);
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) {
    return operator new(n);
}
void operator delete(void* p) noexcept {
    std::free(p);
}
void operator delete[](void* p) noexcept {
    std::free(p);
}
void operator delete(void* p, std::size_t) noexcept {
    std::free(p);
}
void operator delete[](void* p, std::size_t) noexcept {
    std::free(p);
}
