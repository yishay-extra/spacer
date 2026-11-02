#pragma once

#include <memory>

#include "core/Project.h"
#include "engine/GraphBuilder.h"
#include "engine/RecallPlan.h"

namespace spacer {

/// Message-thread side of scene recall (SPEC §11): turns "go from the project's
/// live state to this scene" into a RecallPlan for cmd::RecallScene.
struct RecallRequest {
    /// The live state the engine is in: its sources / speaker chains / master
    /// are the "current scene", its layout is the layout the graph is built for.
    const Project* project{nullptr};
    const Scene* target{nullptr};
    float transitionSeconds{0.0f};
    bool restartMotionPhase{false};
    double sampleRate{48000.0};
    int maxBlockSize{128};
    /// Same factory the live graph was built with (inputs of added sources).
    GraphBuilder::InputFactory inputFactory;
    int numOutputChannels{-1};  // -1 = the layout's
    double atBeat{-1.0};        // >= 0: schedule for that beat
    int token{0};
};

/// Builds the diff and, when sources are added / removed / replaced, the new
/// graph (non-RT, may take a few ms). Does not modify the project: the caller
/// applies the scene to its own model (applyScene) after pushing the plan.
std::unique_ptr<RecallPlan> buildRecallPlan(const RecallRequest& request);

}  // namespace spacer
