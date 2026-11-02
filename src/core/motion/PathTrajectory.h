#pragma once

#include <string_view>
#include <vector>

#include "core/MotionSpec.h"
#include "core/motion/Trajectory.h"

namespace spacer {

/// SPEC §10 `Path`: parametric motion along a polyline or Catmull-Rom spline
/// through Cartesian points.
///
/// - One period is one traversal of the whole path. `Once` and `PingPong`
///   treat the path as open; `Loop` closes it (last point → first point) so
///   the loop has no jump. A closing point equal to the first is dropped.
/// - `CatmullRom` is the uniform Catmull-Rom spline (passes through every
///   point). Open ends use reflected phantom points (2·P0 − P1).
/// - Without `constantSpeed` every segment takes the same time; with it the
///   speed along the curve is constant (arc-length table built in the
///   constructor, kSamplesPerSegment samples per segment).
/// - No points: holds the default Position. One point: holds that point.
///
/// The constructor allocates; compute() does not.
class PathTrajectory final : public Trajectory {
public:
    static constexpr int kSamplesPerSegment = 32;

    explicit PathTrajectory(const PathMotion& spec);

    std::string_view name() const override { return "Path"; }

    /// Total length of the curve in metres (from the arc-length table).
    float length() const { return arc_.empty() ? 0.0f : arc_.back(); }
    int numSegments() const { return segments_; }

    /// Point at curve parameter g in [0, numSegments()] (segment index +
    /// fraction). Exposed for tests and UI path drawing.
    Cartesian pointAt(double g) const;

private:
    Position compute(const MotionTime& t) override;
    void restart() override;
    Cartesian control(int i) const;
    double parameterForProgress(double u) const;

    PathMotion spec_;
    std::vector<Cartesian> points_;
    std::vector<float> arc_;  // cumulative length at g = k / kSamplesPerSegment
    bool closed_{false};
    int segments_{0};
};

}  // namespace spacer
