#pragma once

namespace spacer {

/// Second-order delay-locked loop that filters the timestamps of a nominally
/// periodic event stream (F. Adriaensen, "Using a DLL to filter time", 2005).
/// Used for MIDI clock ticks, MTC quarter frames and audio callback times.
///
/// `update(measured, units)` takes the measured time of an event that lies
/// `units` nominal periods after the previous one and returns the filtered
/// time of that event; `period()` is the filtered time per unit.
///
/// The loop bandwidth is given in Hz. Coefficients are recomputed per update
/// from the update interval (units * period), so irregular update spacing is
/// fine. Plain value type: no allocation, no locks.
class TimeDll {
public:
    explicit TimeDll(double bandwidthHz = 1.0) : bandwidthHz_(bandwidthHz) {}

    void setBandwidth(double bandwidthHz) { bandwidthHz_ = bandwidthHz; }
    double bandwidth() const { return bandwidthHz_; }

    /// Starts the loop at `time` with `periodPerUnit` as the initial period.
    void reset(double time, double periodPerUnit);
    /// Forgets everything; started() is false until the next reset().
    void clear() { started_ = false; }
    bool started() const { return started_; }

    /// Feeds the measured time of the next event, `units` (> 0) periods after
    /// the previous one. `maxError` (> 0) clamps the phase error the loop
    /// reacts to (outlier rejection); 0 = no clamp. Returns the filtered time.
    double update(double measured, double units = 1.0, double maxError = 0.0);

    /// Filtered time of the last event.
    double time() const { return time_; }
    /// Filtered seconds per unit.
    double period() const { return period_; }
    /// Unclamped phase error of the last update (measured - predicted).
    double lastError() const { return lastError_; }
    /// Predicted time `units` periods after the last event.
    double predict(double units) const { return time_ + units * period_; }

private:
    double bandwidthHz_;
    double time_{0.0};
    double period_{0.0};
    double lastError_{0.0};
    bool started_{false};
};

}  // namespace spacer
