#include "core/motion/PathTrajectory.h"

#include <algorithm>
#include <cmath>

namespace spacer {

PathTrajectory::PathTrajectory(const PathMotion& spec) : spec_(spec), points_(spec.points) {
    closed_ = spec_.mode == PlayMode::Loop;
    if (closed_ && points_.size() > 2 && points_.front() == points_.back()) points_.pop_back();
    const int n = static_cast<int>(points_.size());
    segments_ = n < 2 ? 0 : (closed_ ? n : n - 1);

    if (segments_ > 0) {
        const int samples = segments_ * kSamplesPerSegment;
        arc_.resize(static_cast<std::size_t>(samples) + 1);
        arc_[0] = 0.0f;
        Cartesian prev = pointAt(0.0);
        for (int k = 1; k <= samples; ++k) {
            const Cartesian p = pointAt(static_cast<double>(k) / kSamplesPerSegment);
            arc_[static_cast<std::size_t>(k)] = arc_[static_cast<std::size_t>(k - 1)] + (p - prev).length();
            prev = p;
        }
    }
    restart();
}

void PathTrajectory::restart() {
    current_ = compute(MotionTime{}).normalised();
}

Cartesian PathTrajectory::control(int i) const {
    const int n = static_cast<int>(points_.size());
    if (closed_) return points_[static_cast<std::size_t>(((i % n) + n) % n)];
    if (i < 0) return points_[0] * 2.0f - points_[1];
    if (i >= n)
        return points_[static_cast<std::size_t>(n - 1)] * 2.0f - points_[static_cast<std::size_t>(n - 2)];
    return points_[static_cast<std::size_t>(i)];
}

Cartesian PathTrajectory::pointAt(double g) const {
    if (points_.empty()) return Position{}.toCartesian();
    if (segments_ == 0) return points_.front();
    g = std::clamp(g, 0.0, static_cast<double>(segments_));
    int seg = static_cast<int>(std::floor(g));
    if (seg >= segments_) seg = segments_ - 1;
    const auto s = static_cast<float>(g - seg);

    const Cartesian p1 = control(seg);
    const Cartesian p2 = control(seg + 1);
    if (spec_.interpolation == PathInterpolation::Linear) return p1 + (p2 - p1) * s;

    const Cartesian p0 = control(seg - 1);
    const Cartesian p3 = control(seg + 2);
    const float s2 = s * s;
    const float s3 = s2 * s;
    // Uniform Catmull-Rom: 0.5·(2P1 + (−P0+P2)s + (2P0−5P1+4P2−P3)s² + (−P0+3P1−3P2+P3)s³)
    return (p1 * 2.0f + (p2 - p0) * s + (p0 * 2.0f - p1 * 5.0f + p2 * 4.0f - p3) * s2 +
            (p1 * 3.0f - p0 - p2 * 3.0f + p3) * s3) *
           0.5f;
}

double PathTrajectory::parameterForProgress(double u) const {
    if (!spec_.constantSpeed || length() <= 0.0f) return u * segments_;
    const auto target = static_cast<float>(u) * length();
    // First table entry >= target; interpolate within the preceding interval.
    const auto it = std::lower_bound(arc_.begin(), arc_.end(), target);
    if (it == arc_.begin()) return 0.0;
    if (it == arc_.end()) return segments_;
    const auto k = static_cast<double>(it - arc_.begin());
    const float hi = *it;
    const float lo = *(it - 1);
    const double f = hi > lo ? static_cast<double>((target - lo) / (hi - lo)) : 0.0;
    return (k - 1.0 + f) / kSamplesPerSegment;
}

Position PathTrajectory::compute(const MotionTime& t) {
    const double u = playProgress(cyclesOf(t, spec_.period), spec_.mode);
    return Position::fromCartesian(pointAt(parameterForProgress(u)));
}

}  // namespace spacer
