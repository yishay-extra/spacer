#include "engine/RecallPlanner.h"

#include <utility>

namespace spacer {

std::unique_ptr<RecallPlan> buildRecallPlan(const RecallRequest& request) {
    if (request.project == nullptr || request.target == nullptr) return nullptr;
    const Project& project = *request.project;
    const Scene& target = *request.target;

    auto plan = std::make_unique<RecallPlan>();
    plan->transitionSeconds = request.transitionSeconds;
    plan->restartMotionPhase = request.restartMotionPhase;
    plan->atBeat = request.atBeat;
    plan->token = request.token;

    const Scene current = captureScene(project, "");
    plan->diff = diffScenes(current, target, project.layout);

    const bool needsGraph = !plan->diff.addedSources.empty() || !plan->diff.removedSources.empty() ||
                            !plan->diff.replacedSources.empty();
    if (needsGraph) {
        Project next = project;
        applyScene(next, intermediateScene(current, target, plan->diff, project.layout));
        GraphBuilder builder(std::move(next));
        builder.setInputFactory(request.inputFactory);
        if (request.numOutputChannels >= 0) builder.setNumOutputChannels(request.numOutputChannels);
        plan->graph = builder.build(request.sampleRate, request.maxBlockSize);
    }
    return plan;
}

}  // namespace spacer
