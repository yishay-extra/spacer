#pragma once

#include <memory>

#include "core/SceneDiff.h"
#include "engine/Graph.h"

namespace spacer {

/// Everything the audio thread needs to recall a scene (SPEC §11), prepared
/// on the message thread (see CueEngine) and handed over with cmd::RecallScene.
/// Ownership passes to the engine, which returns the plan (and, if the graph
/// was not used, the graph) through its retire queue; collectGarbage() frees it.
///
/// Why a pointer rather than SPEC §5.2's `snapshotIndex`: what has to change
/// depends on the state the engine is in when the cue fires, so the plan is
/// built per recall from the diff against that state.
struct RecallPlan {
    /// Continuous parameters to ramp (`from` is informational; the engine
    /// ramps from the live value) and the structural lists.
    SceneDiff diff;
    float transitionSeconds{0.0f};
    bool restartMotionPhase{false};

    /// Built from intermediateScene(); required iff diff.structural().
    std::unique_ptr<Graph> graph;

    /// >= 0: don't recall now; wait until the transport passes this beat (absolute,
    /// 0 = transport start) and recall at that exact sample. A newer scheduled
    /// plan, or cmd::CancelScheduledRecall, replaces it.
    double atBeat{-1.0};
    /// Reported as fb::CueFired when the plan is applied; 0 = none.
    int token{0};
};

}  // namespace spacer
