#include "core/clock/TimeDll.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace spacer {

void TimeDll::reset(double time, double periodPerUnit) {
    time_ = time;
    period_ = periodPerUnit;
    lastError_ = 0.0;
    started_ = true;
}

double TimeDll::update(double measured, double units, double maxError) {
    if (!started_ || !(units > 0.0)) {
        reset(measured, period_ > 0.0 ? period_ : 0.0);
        return time_;
    }
    const double predicted = time_ + units * period_;
    double e = measured - predicted;
    lastError_ = e;
    if (maxError > 0.0) e = std::clamp(e, -maxError, maxError);

    // omega = 2 pi B T, with T the update interval. Above ~0.7 the loop rings;
    // clamp so a wide bandwidth at a slow event rate stays stable.
    const double interval = units * period_;
    const double omega = std::min(2.0 * std::numbers::pi * bandwidthHz_ * interval, 0.7);
    const double b = std::numbers::sqrt2 * omega;
    const double c = omega * omega;

    time_ = predicted + b * e;
    period_ += c * e / units;
    return time_;
}

}  // namespace spacer
