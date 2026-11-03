#pragma once

#include "core/Motion.h"
#include "core/motion/MotionTimeline.h"
#include "core/motion/ParamOffsets.h"

namespace spacer {

/// Common base of every motion kind (SPEC §10). Handles the time origin,
/// modulation offsets and the readback position; subclasses implement
/// `compute`.
///
/// Time: a fresh instance and every reset() put phase 0 at the start of the
/// next evaluated block. Kinds whose position is a function of time (Orbit,
/// Line, Lissajous, Path, RandomWalk's step index) derive it from the
/// elapsed beats or seconds since that origin, so tempo changes keep them in
/// sync and they never drift. Kinds that are state machines (StepSequence,
/// Manual smoothing) consume the per-block deltas.
///
/// Threads: evaluate() and reset() are both real-time safe (no allocation, no
/// locks) and must be called on the same thread — in the engine that is the
/// audio thread, with reset() arriving through the command queue.
class Trajectory : public Motion {
public:
    void reset() final;
    Position evaluate(const MotionContext& ctx) final;
    Position current() const final { return current_; }

    /// Additive offsets for this block, normally from a ModulatorBank. Fields
    /// this kind does not support are ignored. RT-safe.
    void setModulation(const MotionModulation& m) { modulation_ = m; }
    const MotionModulation& modulation() const { return modulation_; }

    /// Whether this kind reads the given modulation target.
    virtual bool supports(MotionParam) const { return false; }

    /// Adopt `other`'s time origin, so that this motion continues at the
    /// phase `other` had reached instead of starting at phase 0 (an edited
    /// motion of the same kind replacing a running one). State-machine kinds
    /// (StepSequence, RandomWalk targets, Manual glides) keep their own
    /// state. RT-safe.
    void continueTimelineFrom(const Trajectory& other) { timeline_ = other.timeline_; }

    /// After continueTimelineFrom(): the period went from `from` to `to`;
    /// keep the phase reached so far (SPEC §8 re-anchor). RT-safe.
    void reanchorPeriod(const Period& from, const Period& to) { timeline_.reanchor(from, to); }

protected:
    /// Return the position at the end of the block described by `t`.
    virtual Position compute(const MotionTime& t) = 0;

    /// Restart internal state (step 0, reseed, …). Called by reset(). Should
    /// set `current_` to the phase-0 position where that is known.
    virtual void restart() = 0;

    float mod(MotionParam p) const { return modulation_[p]; }
    Position modulated(const Position& p, MotionParam az, MotionParam el, MotionParam dist) const {
        return {p.azimuth + mod(az), p.elevation + mod(el), p.distance + mod(dist)};
    }

    Position current_{};

private:
    MotionTimeline timeline_;
    MotionModulation modulation_{};
};

}  // namespace spacer
