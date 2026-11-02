#pragma once

#include <atomic>

// Global operator new hook for "does not allocate" tests (defined in
// AllocCounter.cpp). Counts allocations only while `g_countAllocs` is set.
namespace spacer::testing {
extern std::atomic<bool> g_countAllocs;
extern std::atomic<long> g_allocs;
}  // namespace spacer::testing
