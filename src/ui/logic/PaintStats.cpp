#include "ui/logic/PaintStats.h"

#include <algorithm>

namespace spacer {

void PaintStats::add(double milliseconds) {
    samples_[static_cast<std::size_t>(next_)] = milliseconds;
    next_ = (next_ + 1) % kWindow;
    count_ = std::min(count_ + 1, kWindow);
}

double PaintStats::meanMs() const {
    if (count_ == 0) return 0.0;
    double sum = 0.0;
    for (int i = 0; i < count_; ++i) sum += samples_[static_cast<std::size_t>(i)];
    return sum / count_;
}

double PaintStats::maxMs() const {
    if (count_ == 0) return 0.0;
    return *std::max_element(samples_.begin(), samples_.begin() + count_);
}

}  // namespace spacer
