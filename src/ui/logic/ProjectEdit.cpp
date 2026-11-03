#include "ui/logic/ProjectEdit.h"

#include <utility>
#include <vector>

#include "ui/logic/SourceGeometry.h"

namespace spacer::edits {

namespace {

void replaceSource(Project& p, const SourceSpec& spec) {
    for (auto& s : p.sources)
        if (s.id == spec.id) {
            s = spec;
            return;
        }
}

/// A Manual motion's smoothing time is read when the node is built; there is
/// no command for it.
float manualSmoothMs(const MotionSpec& m) {
    const auto* manual = std::get_if<ManualMotion>(&m.value);
    return manual != nullptr ? manual->smoothMs : 0.0f;
}

bool manualSmoothingChanged(const SourceSpec& a, const SourceSpec& b) {
    return manualSmoothMs(a.motion) != manualSmoothMs(b.motion) ||
           (a.motionRight && b.motionRight &&
            manualSmoothMs(*a.motionRight) != manualSmoothMs(*b.motionRight));
}

const SourceSpec* findSource(const Project& p, int id) {
    for (const auto& s : p.sources)
        if (s.id == id) return &s;
    return nullptr;
}

}  // namespace

ProjectEdit setLayout(const Project& project, Layout after, std::string name) {
    ProjectEdit e;
    e.name = std::move(name);
    e.apply = [after = std::move(after)](Project& p) {
        p.layout = after;
        std::erase_if(p.speakerChains, [&](const SpeakerChainSpec& c) {
            return p.layout.findSpeaker(c.speakerId) == nullptr;
        });
    };
    e.revert = [before = project.layout, chains = project.speakerChains](Project& p) {
        p.layout = before;
        p.speakerChains = chains;
    };
    e.effect = EngineEffect::structural();
    return e;
}

ProjectEdit setSource(const Project& project, SourceSpec after, std::string name) {
    ProjectEdit e;
    e.name = std::move(name);
    const SourceSpec* before = findSource(project, after.id);
    if (before == nullptr) return e;
    // Structural: anything GraphBuilder bakes into a node. A motion change is
    // structural unless it only moves Manual positions (those travel as
    // SetSourcePosition). Parameter edits (gain, mute, spread, width,
    // distance) are commands for every motion kind, so they never restart a
    // running motion's phase.
    const bool motionChanged = before->motion != after.motion || before->motionRight != after.motionRight;
    const bool positionsOnly =
        isManualOnly(*before) && isManualOnly(after) && !manualSmoothingChanged(*before, after);
    const bool structural = (motionChanged && !positionsOnly) || before->stereo != after.stereo ||
                            before->linkedPoints != after.linkedPoints ||
                            before->spatializer != after.spatializer || !(before->input == after.input) ||
                            before->directSpeakers != after.directSpeakers ||
                            before->modulators != after.modulators || before->inserts != after.inserts;
    e.effect = structural ? EngineEffect::structural() : EngineEffect::source(after.id);
    e.apply = [after = std::move(after)](Project& p) { replaceSource(p, after); };
    e.revert = [b = *before](Project& p) { replaceSource(p, b); };
    return e;
}

ProjectEdit editShow(const Project& project, const std::function<bool(Project&)>& modify, std::string name) {
    ProjectEdit e;
    e.name = std::move(name);
    Project copy = project;
    if (!modify(copy)) return e;
    if (copy.scenes == project.scenes && copy.cueLists == project.cueLists) return e;
    e.apply = [scenes = std::move(copy.scenes), lists = std::move(copy.cueLists)](Project& p) {
        p.scenes = scenes;
        p.cueLists = lists;
    };
    e.revert = [scenes = project.scenes, lists = project.cueLists](Project& p) {
        p.scenes = scenes;
        p.cueLists = lists;
    };
    e.effect = EngineEffect::none();
    return e;
}

}  // namespace spacer::edits
