#pragma once

#include <string_view>
#include <vector>

#include "core/Layout.h"
#include "core/MotionSpec.h"
#include "core/motion/Rng.h"
#include "core/motion/Trajectory.h"

namespace spacer {

/// SPEC §10 `StepSequence`: steps of a given duration, each holding a target
/// (a position or a speaker's position), entered by a jump or a glide.
///
/// - Time is consumed from the per-block deltas of beats and seconds, because
///   steps may mix units. Beats deltas come from ClockState::beat, so
///   beats-based steps follow tempo and freeze on stop. Backwards jumps of the
///   clock are ignored (the sequence never runs backwards). A step that ends
///   inside a block carries the rest of the block into the next step; at most
///   kMaxAdvances steps are taken per block.
/// - `Glide` moves linearly (Cartesian) from wherever the source was when the
///   step began to the target over `glideTime`, then holds. `Jump` is
///   immediate.
/// - Order: `Forward` 0,1,…,n−1; `PingPong` 0,1,…,n−1,n−2,…,1,0,1,… (ends
///   are not repeated); `Random` picks uniformly among the other steps. With
///   `loop == false` the sequence holds its last step after one pass
///   (Forward: n steps, PingPong: there and back, Random: n steps taken).
/// - Probability: each candidate step is taken with its probability, else
///   skipped (no time passes). If n candidates in a row are skipped, the last
///   usable one is taken anyway. Step 0 is always taken after reset().
/// - Speaker targets are resolved against the layout at construction; an
///   unknown speaker (or no layout) makes the step unusable (always skipped).
/// - Deterministic for a fixed seed and sequence of MotionContexts.
///
/// The constructor allocates; compute() and reset() do not.
class StepSequenceTrajectory final : public Trajectory {
public:
    static constexpr int kMaxAdvances = 256;

    StepSequenceTrajectory(const StepSequenceMotion& spec, const Layout* layout);

    std::string_view name() const override { return "StepSequence"; }

    int stepIndex() const { return cur_; }
    bool finished() const { return finished_; }
    /// Number of steps taken since reset (step 0 counts as one).
    long stepsTaken() const { return taken_; }

private:
    struct ResolvedStep {
        Cartesian target{};
        bool usable{false};
        Period duration;
        bool glide{false};
        Period glideTime;
        float probability{1.0f};
    };

    Position compute(const MotionTime& t) override;
    void restart() override;
    int nextCandidate();
    void advance();
    Cartesian positionInStep() const;

    StepSequenceMotion spec_;
    std::vector<ResolvedStep> steps_;
    bool anyUsable_{false};
    Rng rng_;
    int cur_{0};
    int dir_{1};
    long taken_{0};
    bool finished_{false};
    double elapsedBeats_{0.0};
    double elapsedSeconds_{0.0};
    Cartesian stepStart_{};
    Cartesian pos_{};
};

}  // namespace spacer
