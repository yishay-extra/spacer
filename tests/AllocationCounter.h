#pragma once

// Global operator new hook shared by every test that checks real-time code
// does not allocate. Counting is only active inside an AllocationCounter scope.

namespace spacer::test {

class AllocationCounter {
public:
    AllocationCounter();
    ~AllocationCounter();
    AllocationCounter(const AllocationCounter&) = delete;
    AllocationCounter& operator=(const AllocationCounter&) = delete;

    /// Allocations seen since construction.
    long count() const;
};

}  // namespace spacer::test
