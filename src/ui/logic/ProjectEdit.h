#pragma once

// A model edit: what it does to the Project, how to undo it, and what the
// engine has to do afterwards. Plain C++, unit-tested on Linux.
//
// Edits are *scoped*: an edit of one source restores only that source on
// undo, an edit of the layout only the layout. The live project is also
// changed by things that are not edits (cue / scene recalls apply a scene to
// it, SPEC §11), so restoring a whole-project snapshot would silently revert
// those.

#include <functional>
#include <string>

#include "core/Project.h"

namespace spacer {

/// What the engine must do after an edit (or its undo / redo) was applied.
struct EngineEffect {
    enum class Kind {
        None,        // model only (e.g. names)
        Structural,  // rebuild the graph and SwapGraph (SPEC §5.2)
        Source,      // parameters of source `id` changed
        Speaker,     // chain parameters of speaker `id` changed
        Master,      // master gain changed
    };
    Kind kind{Kind::None};
    int id{0};

    static EngineEffect none() { return {}; }
    static EngineEffect structural() { return {Kind::Structural, 0}; }
    static EngineEffect source(int sourceId) { return {Kind::Source, sourceId}; }
    static EngineEffect speaker(int speakerId) { return {Kind::Speaker, speakerId}; }
    static EngineEffect master() { return {Kind::Master, 0}; }
};

struct ProjectEdit {
    std::string name;  // shown as "Undo <name>"
    std::function<void(Project&)> apply;
    std::function<void(Project&)> revert;
    EngineEffect effect;
    /// Edits with the same non-empty key, performed while a gesture is open,
    /// merge into one undo step (e.g. every mouse-move of one drag).
    std::string mergeKey;
};

namespace edits {

/// Replace the layout. `before` is captured from `project` now.
ProjectEdit setLayout(const Project& project, Layout after, std::string name = "Edit Layout");

/// Replace the source with `after.id`. nullopt-like empty edit (apply / revert
/// unset) if the project has no such source.
ProjectEdit setSource(const Project& project, SourceSpec after, std::string name = "Edit Source");

/// Edit of the show data (scenes + cue lists, SPEC §11): `modify` runs on a
/// copy of `project`; the edit stores the before / after of *only* those two
/// collections, so undoing it never reverts live source / speaker state (a
/// recall changes that outside the history). Invalid edit (apply / revert
/// unset) if `modify` returns false or changes nothing. No engine effect: the
/// armed cue list is re-synced by the app.
ProjectEdit editShow(const Project& project, const std::function<bool(Project&)>& modify, std::string name);

/// True if the edit can be performed (it has both functions).
inline bool isValid(const ProjectEdit& e) {
    return static_cast<bool>(e.apply) && static_cast<bool>(e.revert);
}

}  // namespace edits

}  // namespace spacer
