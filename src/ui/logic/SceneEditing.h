#pragma once

// Scene manager operations (SPEC §11, §16 item 3): capture a new scene from
// the live state, update (overwrite) one, rename (cues follow), delete, and
// the "modified" marker of the scene the show is in. Plain C++, unit-tested
// on Linux. Undo is ProjectHistory's job (edits::editShow wraps these).

#include <string>
#include <vector>

#include "core/Project.h"

namespace spacer::sceneedit {

int indexOfScene(const Project& project, const std::string& name);
/// `base`, or `base 2`, `base 3` ... not used by another scene.
std::string uniqueSceneName(const Project& project, const std::string& base);

/// Adds a scene captured from the live state. False if the name is empty or taken.
bool captureNew(Project& project, const std::string& name);
/// "Update scene": overwrite the scene with the live state. False if it does not exist.
bool update(Project& project, const std::string& name);
/// Renames the scene and every cue that recalls it. False if `from` does not
/// exist or `to` is empty or taken by another scene.
bool rename(Project& project, const std::string& from, const std::string& to);
/// Removes the scene. Cues that recall it are kept (they show as "missing";
/// a recall of a missing scene is an error the cue engine reports).
bool remove(Project& project, const std::string& name);

struct CueRef {
    std::size_t list{0};
    std::size_t cue{0};
};
/// Every cue (any list) that recalls `sceneName`.
std::vector<CueRef> cuesUsing(const Project& project, const std::string& sceneName);

/// The scene the show is in, as far as the live state is concerned (SPEC §11:
/// "changes are applied immediately and marked modified").
struct LiveStatus {
    bool known{false};     // `sceneName` names an existing scene
    bool modified{false};  // live state differs from the stored scene
};
LiveStatus liveStatus(const Project& project, const std::string& sceneName);

}  // namespace spacer::sceneedit
