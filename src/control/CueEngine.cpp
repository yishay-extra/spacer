#include "control/CueEngine.h"

#include <algorithm>
#include <utility>

#include "core/SceneDiff.h"
#include "engine/PluginHost.h"
#include "engine/RecallPlanner.h"

namespace spacer {

CueEngine::CueEngine(EngineProcessor& engine, Project& project, Config config)
    : engine_(engine), project_(project), config_(std::move(config)), sequencer_({}, config_.randomSeed) {}

bool CueEngine::arm(const std::string& name, const CueTick& tick) {
    for (const auto& list : project_.cueLists) {
        if (list.name != name) continue;
        sequencer_.setList(list);
        sequencer_.arm(tick);
        armed_ = true;
        armedIndex_ = static_cast<int>(&list - project_.cueLists.data());
        armedCount_ = project_.cueLists.size();
        scheduleBeatTrigger(tick);
        return true;
    }
    return false;
}

bool CueEngine::armFirst(const CueTick& tick) {
    return !project_.cueLists.empty() && arm(project_.cueLists.front().name, tick);
}

bool CueEngine::refreshArmedList(const CueTick& tick) {
    if (!armed_) return false;
    const CueList* found = nullptr;
    for (std::size_t i = 0; i < project_.cueLists.size(); ++i)
        if (project_.cueLists[i].name == sequencer_.list().name) {
            found = &project_.cueLists[i];
            armedIndex_ = static_cast<int>(i);
            break;
        }
    if (found == nullptr && armedIndex_ >= 0 && project_.cueLists.size() == armedCount_ &&
        static_cast<std::size_t>(armedIndex_) < project_.cueLists.size())
        found = &project_.cueLists[static_cast<std::size_t>(armedIndex_)];  // renamed
    armedCount_ = project_.cueLists.size();
    if (found == nullptr) {
        if (beatScheduled_) engine_.push(cmd::CancelScheduledRecall{});
        beatScheduled_ = false;
        armed_ = false;
        armedIndex_ = -1;
        return true;
    }
    if (*found == sequencer_.list()) return false;
    sequencer_.updateList(*found, tick);
    if (beatScheduled_) {
        engine_.push(cmd::CancelScheduledRecall{});
        beatScheduled_ = false;
    }
    scheduleBeatTrigger(tick);
    return true;
}

bool CueEngine::recall(const Scene& scene, float seconds, bool restart, double atBeat, int token) {
    // Plugin edits made in editors are part of the current state (SPEC §11).
    if (config_.pluginHost != nullptr) config_.pluginHost->captureState(project_);
    RecallRequest r;
    r.project = &project_;
    r.target = &scene;
    r.transitionSeconds = seconds;
    r.restartMotionPhase = restart;
    r.sampleRate = config_.sampleRate;
    r.maxBlockSize = config_.maxBlockSize;
    r.inputFactory = config_.inputFactory;
    r.pluginHost = config_.pluginHost;
    r.numOutputChannels = config_.numOutputChannels;
    r.atBeat = atBeat;
    r.token = token;
    auto plan = buildRecallPlan(r);
    if (!plan) return false;
    if (!engine_.push(cmd::RecallScene{plan.get()})) {
        ++dropped_;
        return false;  // plan is freed here; the engine never saw it
    }
    plan.release();
    return true;
}

bool CueEngine::fire(int cueIndex, const CueTick& tick) {
    const Cue& cue = sequencer_.list().cues[static_cast<std::size_t>(cueIndex)];
    // Whatever was waiting for a beat belonged to the previous "next" cue.
    if (beatScheduled_) {
        engine_.push(cmd::CancelScheduledRecall{});
        beatScheduled_ = false;
    }
    lastError_.clear();
    if (const Scene* scene = findScene(project_, cue.sceneName)) {
        const Scene target = *scene;  // the recall must not alias the live model
        recall(target, cue.transitionSeconds, cue.restartMotionPhase, -1.0, 0);
        applyScene(project_, target);
        ++recallCount_;
        lastRecalled_ = target.name;
    } else {
        lastError_ = "cue \"" + cue.name + "\": scene \"" + cue.sceneName + "\" not found";
    }
    scheduleBeatTrigger(tick);
    return true;
}

void CueEngine::scheduleBeatTrigger(const CueTick& tick) {
    const auto pending = sequencer_.pendingBeatTrigger(tick.clock.beatsPerBar);
    if (!pending) return;
    const Cue& cue = sequencer_.list().cues[static_cast<std::size_t>(pending->cueIndex)];
    const Scene* scene = findScene(project_, cue.sceneName);
    if (scene == nullptr) return;
    const Scene target = *scene;
    beatScheduled_ =
        recall(target, cue.transitionSeconds, cue.restartMotionPhase, pending->beat, pending->cueIndex + 1);
}

bool CueEngine::go(const CueTick& tick) {
    if (!armed_) return false;
    const auto i = sequencer_.go(tick);
    return i && fire(*i, tick);
}

bool CueEngine::back(const CueTick& tick) {
    if (!armed_) return false;
    const auto i = sequencer_.back(tick);
    return i && fire(*i, tick);
}

bool CueEngine::jump(int cueIndex, const CueTick& tick) {
    if (!armed_) return false;
    const auto i = sequencer_.jump(cueIndex, tick);
    return i && fire(*i, tick);
}

bool CueEngine::tick(const CueTick& t) {
    if (!armed_) return false;
    const auto i = sequencer_.update(t);
    return i && fire(*i, t);
}

bool CueEngine::onOsc(const std::string& address, const CueTick& t) {
    if (!armed_) return false;
    const auto i = sequencer_.onOsc(address, t);
    return i && fire(*i, t);
}

bool CueEngine::onMidi(trigger::Midi::Type type, int number, int channel, const CueTick& t) {
    if (!armed_) return false;
    const auto i = sequencer_.onMidi(type, number, channel, t);
    return i && fire(*i, t);
}

bool CueEngine::onEngineCueFired(int token, const CueTick& t) {
    if (!armed_ || token <= 0) return false;
    const int index = token - 1;
    if (!sequencer_.beatFired(index, t)) return false;  // stale: a Go got there first
    beatScheduled_ = false;
    const Cue& cue = sequencer_.list().cues[static_cast<std::size_t>(index)];
    if (const Scene* scene = findScene(project_, cue.sceneName)) {
        applyScene(project_, *scene);
        ++recallCount_;
        lastRecalled_ = scene->name;
    }
    scheduleBeatTrigger(t);
    return true;
}

bool CueEngine::recallScene(const std::string& sceneName, float seconds, bool restart) {
    const Scene* scene = findScene(project_, sceneName);
    if (scene == nullptr) return false;
    const Scene target = *scene;
    const bool ok = recall(target, seconds, restart, -1.0, 0);
    if (ok) {
        applyScene(project_, target);
        ++recallCount_;
        lastRecalled_ = target.name;
    }
    return ok;
}

void CueEngine::storeScene(const std::string& name) {
    Scene snap = captureScene(project_, name);
    for (auto& s : project_.scenes)
        if (s.name == name) {
            s = std::move(snap);
            return;
        }
    project_.scenes.push_back(std::move(snap));
}

}  // namespace spacer
