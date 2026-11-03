#pragma once

// "Update scene" / "New scene" (SPEC §11, §13): the live plug-in state is
// captured into the model *first*, so edits made in plug-in editors end up in
// the stored scene. Plain C++, unit-tested on Linux; the app passes
// PluginHost::captureState as `capture`.

#include <functional>
#include <string>

#include "core/Project.h"
#include "ui/logic/ProjectEdit.h"

namespace spacer::scenecapture {

/// Writes live state that lives outside the model (plug-in instances) into the
/// project; returns the number of slots updated.
using CaptureLive = std::function<int(Project&)>;

/// Captures into `live`, then builds the edit that overwrites scene `name`
/// with the live state. Invalid edit (edits::isValid false) if the scene does
/// not exist or nothing differs. The capture itself stays even then.
ProjectEdit updateScene(Project& live, const std::string& name, const CaptureLive& capture);
/// Same for a new scene (invalid if the name is empty or taken).
ProjectEdit newScene(Project& live, const std::string& name, const CaptureLive& capture);

}  // namespace spacer::scenecapture
