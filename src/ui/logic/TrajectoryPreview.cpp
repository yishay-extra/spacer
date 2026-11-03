#include "ui/logic/TrajectoryPreview.h"

#include <algorithm>
#include <cmath>
#include <type_traits>

#include "core/motion/MotionFactory.h"

namespace spacer {

namespace {

double toSeconds(const Period& p, double bpm) {
    const double v = std::max(0.0, p.value);
    return p.unit == Period::Unit::Seconds ? v : v * 60.0 / std::max(bpm, 1.0);
}

double modeFactor(PlayMode mode) {
    return mode == PlayMode::PingPong ? 2.0 : 1.0;
}

ClockState runningClock(double bpm, double seconds, double sampleRate) {
    ClockState c;
    c.bpm = std::max(bpm, 1.0);
    c.playing = true;
    c.sampleRate = sampleRate;
    c.timeSeconds = seconds;
    c.beat = seconds * c.bpm / 60.0;
    return c;
}

}  // namespace

double previewCycleSeconds(const MotionSpec& motion, const Layout& /*layout*/, double bpm,
                           int randomWalkSteps) {
    return std::visit(
        [&](const auto& m) -> double {
            using T = std::decay_t<decltype(m)>;
            if constexpr (std::is_same_v<T, ManualMotion>) {
                return 0.0;
            } else if constexpr (std::is_same_v<T, OrbitMotion> || std::is_same_v<T, LissajousMotion>) {
                return toSeconds(m.period, bpm);
            } else if constexpr (std::is_same_v<T, LineMotion> || std::is_same_v<T, PathMotion>) {
                return toSeconds(m.period, bpm) * modeFactor(m.mode);
            } else if constexpr (std::is_same_v<T, RandomWalkMotion>) {
                return toSeconds(m.stepPeriod, bpm) * std::max(1, randomWalkSteps);
            } else {
                double total = 0.0;
                for (const auto& s : m.steps) total += toSeconds(s.duration, bpm);
                return m.order == StepOrder::PingPong ? total * 2.0 : total;
            }
        },
        motion.value);
}

std::vector<Cartesian> previewTrajectory(const MotionSpec& motion, const Layout& layout,
                                         const TrajectoryPreviewOptions& options) {
    std::vector<Cartesian> out;
    if (std::holds_alternative<ManualMotion>(motion.value)) return out;
    double cycle = previewCycleSeconds(motion, layout, options.bpm, options.randomWalkSteps);
    if (!std::isfinite(cycle) || cycle <= 0.0) return out;
    cycle = std::min(cycle, options.maxSeconds);

    auto trajectory = MotionFactory::create(motion, &layout);
    if (!trajectory) return out;

    const int n = std::max(2, options.numPoints);
    constexpr int kBlock = 64;  // samples per evaluated step
    const double dt = cycle / (n - 1);
    const double sampleRate = kBlock / dt;
    out.reserve(static_cast<std::size_t>(n));

    // A zero-length first block gives the phase-0 position.
    out.push_back(
        trajectory->evaluate({runningClock(options.bpm, 0.0, sampleRate), 0, sampleRate}).toCartesian());
    for (int i = 1; i < n; ++i) {
        const MotionContext ctx{runningClock(options.bpm, (i - 1) * dt, sampleRate), kBlock, sampleRate};
        out.push_back(trajectory->evaluate(ctx).toCartesian());
    }
    return out;
}

Position motionStartPosition(const MotionSpec& motion, const Layout& layout) {
    if (const auto* m = std::get_if<ManualMotion>(&motion.value)) return m->position;
    auto trajectory = MotionFactory::create(motion, &layout);
    if (!trajectory) return {};
    return trajectory->evaluate({runningClock(120.0, 0.0, 48000.0), 0, 48000.0});
}

}  // namespace spacer
