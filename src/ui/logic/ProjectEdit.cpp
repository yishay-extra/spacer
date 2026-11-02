#include "ui/logic/ProjectEdit.h"

#include <utility>

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

const SourceSpec* findSource(const Project& p, int id) {
    for (const auto& s : p.sources)
        if (s.id == id) return &s;
    return nullptr;
}

}  // namespace

ProjectEdit setLayout(const Project& project, Layout after, std::string name) {
    ProjectEdit e;
    e.name = std::move(name);
    e.apply = [after = std::move(after)](Project& p) { p.layout = after; };
    e.revert = [before = project.layout](Project& p) { p.layout = before; };
    e.effect = EngineEffect::structural();
    return e;
}

ProjectEdit setSource(const Project& project, SourceSpec after, std::string name) {
    ProjectEdit e;
    e.name = std::move(name);
    const SourceSpec* before = findSource(project, after.id);
    if (before == nullptr) return e;
    // A motion-kind change, or any change to a non-Manual motion, needs a new
    // graph (motions are built by GraphBuilder); Manual positions and plain
    // parameters travel as commands.
    const bool structural = !isManualOnly(*before) || !isManualOnly(after) ||
                            before->stereo != after.stereo || before->linkedPoints != after.linkedPoints ||
                            before->spatializer != after.spatializer || !(before->input == after.input) ||
                            before->directSpeakers != after.directSpeakers ||
                            before->modulators != after.modulators || before->inserts != after.inserts;
    e.effect = structural ? EngineEffect::structural() : EngineEffect::source(after.id);
    e.apply = [after = std::move(after)](Project& p) { replaceSource(p, after); };
    e.revert = [b = *before](Project& p) { replaceSource(p, b); };
    return e;
}

}  // namespace spacer::edits
