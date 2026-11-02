#include "AllocationCounter.h"

#include <atomic>
#include <cstdlib>
#include <new>

namespace {
std::atomic<bool> g_countAllocs{false};
std::atomic<long> g_allocs{0};
}  // namespace

void* operator new(std::size_t n) {
    if (g_countAllocs.load(std::memory_order_relaxed)) g_allocs.fetch_add(1, std::memory_order_relaxed);
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

namespace spacer::test {

AllocationCounter::AllocationCounter() {
    g_allocs = 0;
    g_countAllocs = true;
}
AllocationCounter::~AllocationCounter() {
    g_countAllocs = false;
}
long AllocationCounter::count() const {
    return g_allocs.load();
}

}  // namespace spacer::test
