#pragma once

// Rolling window of paint times for the opt-in stage readout. Plain C++.

#include <array>

namespace spacer {

/// Mean and maximum of the last kWindow paint times.
class PaintStats {
public:
    static constexpr int kWindow = 60;
    void add(double milliseconds);
    double meanMs() const;  // 0 when empty
    double maxMs() const;   // 0 when empty
    int count() const { return count_; }

private:
    std::array<double, kWindow> samples_{};
    int next_{0};
    int count_{0};
};

}  // namespace spacer
