#pragma once

// Trajectory previews for the stage view ("trajectories drawn as faint
// paths", SPEC §16 item 1): a motion is evaluated offline, with a simulated
// running transport, over one cycle. Plain C++, unit-tested on Linux. Not
// real-time: allocates; call on the message thread when a spec changes.

#include <vector>

#include "core/Layout.h"
#include "core/MotionSpec.h"

namespace spacer {

struct TrajectoryPreviewOptions {
    double bpm{120.0};
    int numPoints{160};
    double maxSeconds{120.0};  // cap for very long cycles
    int randomWalkSteps{16};   // RandomWalk has no cycle: preview this many steps
};

/// Length in seconds of one drawn cycle of `motion` at `bpm`: the period
/// (twice the period for ping-pong), the sum of step durations for a step
/// sequence, `randomWalkSteps` steps for a random walk; 0 for Manual. Not capped.
double previewCycleSeconds(const MotionSpec& motion, const Layout& layout, double bpm,
                           int randomWalkSteps = 16);

/// Positions (Cartesian, SPEC §2) along one cycle, starting with the phase-0
/// position. Empty for Manual (it does not move by itself). Deterministic.
std::vector<Cartesian> previewTrajectory(const MotionSpec& motion, const Layout& layout,
                                         const TrajectoryPreviewOptions& options = {});

/// The phase-0 position of `motion` (Manual: its position).
Position motionStartPosition(const MotionSpec& motion, const Layout& layout);

}  // namespace spacer
