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
    /// Summed offset for one insert parameter (`insert.{slot}.param.{index}`).
    struct InsertOutput {
        int slot{0};
        int index{0};
        float value{0.0f};
    };
    struct Output {
        SourceModulation source;
        MotionModulation motion;       // `motion.*`
        MotionModulation motionRight;  // `motionRight.*` (linkedPoints right point)
        std::array<InsertOutput, kMaxModulators> inserts{};
        int numInserts{0};
    };

    /// One modulator in a trivially copyable form (travels through the
    /// engine command queue as cmd::SetModulator). The osc / midi source's
    /// address is not needed here: external values arrive by index.
    struct Live {
        bool lfo{true};
        LfoShape shape{LfoShape::Sine};
        Period period{Period::beats(4)};
        bool targetValid{false};
        ParameterPath target;
        float depth{1.0f};
        float offset{0.0f};
        static Live fromSpec(const ModulatorSpec& spec);
    };

    void configure(const std::vector<ModulatorSpec>& specs);

    /// Restart every LFO's phase at the next process().
    void reset();

    /// Adopt `other`'s LFO timeline, so modulators replacing running ones
    /// continue: each LFO keeps the phase the one at its index had reached
    /// (also across a period change, re-anchored like motions, SPEC §8), and
    /// external sources keep their last value. RT-safe.
    void continueTimelineFrom(const ModulatorBank& other);

    /// Live edit (RT): replace modulator `index`, or append it when `index`
    /// == size(); `count` (<= kMaxModulators) is the number of modulators
    /// afterwards. An edited LFO keeps its phase; an appended one starts at
    /// phase 0. Indices past `count` are dropped; `index` < 0 only drops
    /// (truncates to `count`).
    void setLive(int index, const Live& m, int count);

    /// True if any active modulator targets `p`.
    bool targets(SourceParam p) const;

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
        double cycleOffset{0.0};  // LFO phase continuity across edits (cycles)
        LfoShape shape{LfoShape::Sine};
        Period period;
        bool valid{false};
        ParameterPath target;
        float depth{0.0f};
        float offset{0.0f};
        float external{0.0f};
        float last{0.0f};
    };

    static Slot slotFrom(const Live& m);
    /// Cycle offset that keeps `next` at the phase `prev` has at last_.
    double continuedOffset(const Slot& prev, const Slot& next) const;

    std::array<Slot, kMaxModulators> slots_{};
    int count_{0};
    MotionTimeline timeline_;
    MotionTime last_{};  // time of the last process() (end of that block)
};

}  // namespace spacer
