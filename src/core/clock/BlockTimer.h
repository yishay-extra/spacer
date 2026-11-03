#pragma once

#include "core/clock/TimeDll.h"

namespace spacer {

/// Filters the host time at which audio callbacks run into a smooth
/// sample-count → host-time mapping (DLL, SPEC §9 external clocks). The audio
/// thread reads the host clock at the top of each callback; that reading
/// jitters by up to a millisecond or so, the sample count does not.
///
/// `blockStart(now, numSamples)` returns the filtered host time of the first
/// sample of the block that starts now. A reading that disagrees with the
/// prediction by more than kResetSeconds (device restart, long stall)
/// restarts the filter. No allocation; audio thread.
class BlockTimer {
public:
    static constexpr double kBandwidthHz = 0.5;
    static constexpr double kResetSeconds = 0.05;

    void prepare(double sampleRate);
    double blockStart(double now, int numSamples);
    /// Filtered seconds per sample (≈ 1 / sampleRate, in host time).
    double secondsPerSample() const { return dll_.started() ? dll_.period() : 1.0 / sampleRate_; }
    void reset() { dll_.clear(); }

private:
    TimeDll dll_{kBandwidthHz};
    double sampleRate_{48000.0};
    int previousSamples_{0};
};

}  // namespace spacer
