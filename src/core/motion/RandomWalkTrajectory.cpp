#include "core/motion/RandomWalkTrajectory.h"

#include <algorithm>
#include <cmath>

namespace spacer {

namespace {
constexpr float kEps = 1e-5f;
}

RandomWalkTrajectory::RandomWalkTrajectory(const RandomWalkMotion& spec) : spec_(spec) {
    restart();
}

bool RandomWalkTrajectory::contains(const Cartesian& p) const {
    if (const auto* box = std::get_if<BoxBounds>(&spec_.bounds))
        return p.x >= box->min.x - kEps && p.x <= box->max.x + kEps && p.y >= box->min.y - kEps &&
               p.y <= box->max.y + kEps && p.z >= box->min.z - kEps && p.z <= box->max.z + kEps;
    const auto& s = std::get<SphereBounds>(spec_.bounds);
    return (p - s.centre).length() <= s.radius + kEps;
}

Cartesian RandomWalkTrajectory::clampInside(const Cartesian& p) const {
    if (const auto* box = std::get_if<BoxBounds>(&spec_.bounds))
        return {std::clamp(p.x, box->min.x, box->max.x), std::clamp(p.y, box->min.y, box->max.y),
                std::clamp(p.z, box->min.z, box->max.z)};
    const auto& s = std::get<SphereBounds>(spec_.bounds);
    const Cartesian d = p - s.centre;
    const float len = d.length();
    return len <= s.radius ? p : s.centre + d * (s.radius / len);
}

Cartesian RandomWalkTrajectory::randomPoint() {
    if (const auto* box = std::get_if<BoxBounds>(&spec_.bounds))
        return {rng_.uniform(box->min.x, box->max.x), rng_.uniform(box->min.y, box->max.y),
                rng_.uniform(box->min.z, box->max.z)};
    const auto& s = std::get<SphereBounds>(spec_.bounds);
    for (int i = 0; i < 16; ++i) {
        const Cartesian d{rng_.uniform(-1.0f, 1.0f), rng_.uniform(-1.0f, 1.0f), rng_.uniform(-1.0f, 1.0f)};
        if (d.length() <= 1.0f) return s.centre + d * s.radius;
    }
    return s.centre;
}

Cartesian RandomWalkTrajectory::randomDirection() {
    const float z = rng_.uniform(-1.0f, 1.0f);
    const float phi = rng_.uniform(0.0f, 2.0f * kPi);
    const float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
    Cartesian d{r * std::cos(phi), r * std::sin(phi), z};
    if (const auto* box = std::get_if<BoxBounds>(&spec_.bounds)) {
        if (box->max.x - box->min.x <= 0.0f) d.x = 0.0f;
        if (box->max.y - box->min.y <= 0.0f) d.y = 0.0f;
        if (box->max.z - box->min.z <= 0.0f) d.z = 0.0f;
        const float len = d.length();
        d = len > 0.0f ? d * (1.0f / len) : Cartesian{};
    }
    return d;
}

Cartesian RandomWalkTrajectory::step(const Cartesian& p) {
    const float len = std::max(0.0f, spec_.stepMetres + mod(MotionParam::StepMetres));
    Cartesian q = p;
    for (int i = 0; i < kMaxRetries; ++i) {
        q = p + randomDirection() * len;
        if (contains(q)) return q;
    }
    return clampInside(q);
}

void RandomWalkTrajectory::restart() {
    rng_.reseed(spec_.seed);
    index_ = 0;
    from_ = randomPoint();
    to_ = step(from_);
    current_ = Position::fromCartesian(spec_.glide ? from_ : to_).normalised();
}

Position RandomWalkTrajectory::compute(const MotionTime& t) {
    const double cycles = cyclesOf(t, spec_.stepPeriod);
    // Clamp before converting so a huge jump cannot overflow the integer.
    const auto k = static_cast<std::int64_t>(std::floor(std::clamp(cycles, -1e15, 1e15)));
    if (k != index_) {
        const std::int64_t delta = k > index_ ? k - index_ : index_ - k;
        const auto n = static_cast<int>(std::min<std::int64_t>(delta, kMaxCatchUp));
        for (int i = 0; i < n; ++i) {
            from_ = to_;
            to_ = step(to_);
        }
        index_ = k;
    }
    if (!spec_.glide) return Position::fromCartesian(to_);
    const auto f = static_cast<float>(fractional(cycles));
    return Position::fromCartesian(from_ + (to_ - from_) * f);
}

}  // namespace spacer
