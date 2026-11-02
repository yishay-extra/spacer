#pragma once

#include <cstdint>
#include <string_view>

#include "core/MotionSpec.h"
#include "core/motion/Rng.h"
#include "core/motion/Trajectory.h"

namespace spacer {

/// SPEC §10 `RandomWalk`: a new target every `stepPeriod`, `stepMetres` away
/// from the previous one in a random direction, kept inside `bounds`.
///
/// - The step index is floor(t / stepPeriod), derived from the clock like the
///   other periodic kinds, so beats-based walks follow tempo and freeze on
///   stop.
/// - With `glide` the position moves linearly from the previous target to the
///   current one over the step; without it, it jumps at the step start.
/// - The walk starts at a seeded random point inside the bounds. A step that
///   would leave the bounds is redrawn (up to kMaxRetries directions), then
///   clamped. Box axes with zero extent are kept fixed, so a flat box gives a
///   planar walk.
/// - If the index changes by more than kMaxCatchUp steps in one block (e.g.
///   setBeat), only kMaxCatchUp steps are taken. A backwards jump also walks
///   forward: the walk never revisits old targets.
/// - Deterministic for a given seed and sequence of MotionContexts.
class RandomWalkTrajectory final : public Trajectory {
public:
    static constexpr int kMaxRetries = 8;
    static constexpr int kMaxCatchUp = 64;

    explicit RandomWalkTrajectory(const RandomWalkMotion& spec);

    std::string_view name() const override { return "RandomWalk"; }
    bool supports(MotionParam p) const override { return p == MotionParam::StepMetres; }

    /// Current target and the one before it (Cartesian), for tests and UI.
    const Cartesian& target() const { return to_; }
    const Cartesian& previousTarget() const { return from_; }
    bool contains(const Cartesian& p) const;

private:
    Position compute(const MotionTime& t) override;
    void restart() override;
    Cartesian randomPoint();
    Cartesian randomDirection();
    Cartesian clampInside(const Cartesian& p) const;
    Cartesian step(const Cartesian& p);

    RandomWalkMotion spec_;
    Rng rng_;
    std::int64_t index_{0};
    Cartesian from_{};
    Cartesian to_{};
};

}  // namespace spacer
