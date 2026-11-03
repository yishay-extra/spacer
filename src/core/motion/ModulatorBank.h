#pragma once

#include <array>
#include <vector>

#include "core/Motion.h"
#include "core/MotionSpec.h"
#include "core/Source.h"
#include "core/motion/MotionTimeline.h"
#include "core/motion/ParamOffsets.h"
#include "core/motion/ParameterPath.h"

namespace spacer {

class Trajectory;

/// Up to kMaxModulators modulators of one source (SPEC §10).
///
/// Each modulator's output is `offset + depth · s`, where s is
///   - lfo:  lfoValue(shape, t / period) in [−1, 1], phase from the clock
///           (beats or seconds, like motions; reset() restarts it);
///   - osc:  the last value given to setExternal(), as received;
///   - midi: the last value given to setExternal(), normalised to 0..1
///           (cc / 127) by the caller.
/// Outputs are summed per target parameter path and applied additively to
/// the spec values, in the parameter's own units. Unknown paths and
/// modulators beyond kMaxModulators are inactive.
///
/// Typical per-block use on the audio thread, before the motion:
///   const SourceModulation srcMod = bank.applyTo(motion, ctx);
///   const Position p = motion.evaluate(ctx);
///
/// configure() allocates nothing but is non-RT by convention (it is a model
/// change); everything else is RT-safe and must run on the audio thread.
class ModulatorBank {
public:
    struct Output {
        SourceModulation source;
        MotionModulation motion;
    };

    void configure(const std::vector<ModulatorSpec>& specs);

    /// Restart every LFO's phase at the next process().
    void reset() { timeline_.restart(); }

    /// Adopt `other`'s LFO time origin (edited modulators replacing running
    /// ones keep their phase). RT-safe.
    void continueTimelineFrom(const ModulatorBank& other) { timeline_ = other.timeline_; }

    /// Feed an external (osc / midi) source value. Ignored for out-of-range
    /// indices and LFO modulators.
    void setExternal(int index, float value);

    /// Compute every modulator at the END of the block described by ctx
    /// (matching Motion::evaluate) and sum per target.
    Output process(const MotionContext& ctx);

    /// process(), hand the motion offsets to `motion`, return the source ones.
    SourceModulation applyTo(Trajectory& motion, const MotionContext& ctx);

    int size() const { return count_; }
    bool active(int index) const;
    /// Last output of modulator `index` (offset + depth·s), for UI/OSC readback.
    float value(int index) const;

private:
    enum class Kind { Lfo, External };
    struct Slot {
        Kind kind{Kind::Lfo};
        LfoShape shape{LfoShape::Sine};
        Period period;
        bool valid{false};
        ParameterPath target;
        float depth{0.0f};
        float offset{0.0f};
        float external{0.0f};
        float last{0.0f};
    };

    std::array<Slot, kMaxModulators> slots_{};
    int count_{0};
    MotionTimeline timeline_;
};

}  // namespace spacer
