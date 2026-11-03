#include "core/clock/BlockTimer.h"

#include <cmath>

namespace spacer {

void BlockTimer::prepare(double sampleRate) {
    if (sampleRate > 0.0) sampleRate_ = sampleRate;
    dll_.clear();
    previousSamples_ = 0;
}

double BlockTimer::blockStart(double now, int numSamples) {
    double t = now;
    if (!dll_.started() || previousSamples_ <= 0) {
        dll_.reset(now, 1.0 / sampleRate_);
    } else {
        const double predicted = dll_.predict(previousSamples_);
        if (std::abs(now - predicted) > kResetSeconds) {
            dll_.reset(now, 1.0 / sampleRate_);
        } else {
            t = dll_.update(now, previousSamples_);
        }
    }
    previousSamples_ = numSamples;
    return t;
}

}  // namespace spacer
