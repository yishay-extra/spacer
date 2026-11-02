#include "ui/logic/SourceGeometry.h"

#include <type_traits>

#include "ui/logic/TrajectoryPreview.h"

namespace spacer {

int sourcePointCount(const SourceSpec& source) {
    return source.stereo ? 2 : 1;
}

const MotionSpec& motionForChannel(const SourceSpec& source, int channel) {
    if (channel == 1 && source.stereo && source.linkedPoints && source.motionRight)
        return *source.motionRight;
    return source.motion;
}

Position stereoPoint(const Position& centre, float widthDeg, int channel, bool stereo) {
    if (!stereo) return centre;
    const float half = widthDeg * 0.5f;
    Position p = centre;
    p.azimuth = Position::wrapAzimuth(centre.azimuth + (channel == 0 ? -half : half));
    return p;
}

std::vector<Position> modelPoints(const SourceSpec& source, const Layout& layout) {
    std::vector<Position> out;
    const int n = sourcePointCount(source);
    for (int ch = 0; ch < n; ++ch) {
        const Position p = motionStartPosition(motionForChannel(source, ch), layout);
        // linkedPoints: each point has its own motion and no width offset.
        out.push_back(source.linkedPoints ? p : stereoPoint(p, source.width, ch, source.stereo));
    }
    return out;
}

Position offsetPosition(const Position& p, const Cartesian& delta) {
    return Position::fromCartesian(p.toCartesian() + delta);
}

namespace {

Cartesian offsetCartesian(const Cartesian& c, const Cartesian& delta) {
    return c + delta;
}

}  // namespace

MotionSpec offsetMotion(const MotionSpec& motion, const Cartesian& delta) {
    MotionSpec out = motion;
    std::visit(
        [&](auto& m) {
            using T = std::decay_t<decltype(m)>;
            if constexpr (std::is_same_v<T, ManualMotion>) {
                m.position = offsetPosition(m.position, delta);
            } else if constexpr (std::is_same_v<T, OrbitMotion> || std::is_same_v<T, LissajousMotion>) {
                m.centre = offsetPosition(m.centre, delta);
            } else if constexpr (std::is_same_v<T, LineMotion>) {
                m.from = offsetPosition(m.from, delta);
                m.to = offsetPosition(m.to, delta);
            } else if constexpr (std::is_same_v<T, PathMotion>) {
                for (auto& p : m.points) p = offsetCartesian(p, delta);
            } else if constexpr (std::is_same_v<T, RandomWalkMotion>) {
                std::visit(
                    [&](auto& b) {
                        using B = std::decay_t<decltype(b)>;
                        if constexpr (std::is_same_v<B, BoxBounds>) {
                            b.min = offsetCartesian(b.min, delta);
                            b.max = offsetCartesian(b.max, delta);
                        } else {
                            b.centre = offsetCartesian(b.centre, delta);
                        }
                    },
                    m.bounds);
            } else if constexpr (std::is_same_v<T, StepSequenceMotion>) {
                for (auto& step : m.steps)
                    if (auto* p = std::get_if<Position>(&step.target)) *p = offsetPosition(*p, delta);
            }
        },
        out.value);
    return out;
}

SourceSpec dragSource(const SourceSpec& start, int channel, const Cartesian& delta) {
    SourceSpec out = start;
    if (start.stereo && start.linkedPoints && channel == 1) {
        out.motionRight = offsetMotion(start.motionRight ? *start.motionRight : start.motion, delta);
    } else {
        out.motion = offsetMotion(start.motion, delta);
    }
    return out;
}

bool isManualOnly(const SourceSpec& source) {
    if (!std::holds_alternative<ManualMotion>(source.motion.value)) return false;
    if (source.stereo && source.linkedPoints && source.motionRight)
        return std::holds_alternative<ManualMotion>(source.motionRight->value);
    return true;
}

}  // namespace spacer
