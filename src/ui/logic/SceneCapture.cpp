#include "ui/logic/SceneCapture.h"

#include "ui/logic/SceneEditing.h"

namespace spacer::scenecapture {

ProjectEdit updateScene(Project& live, const std::string& name, const CaptureLive& capture) {
    if (capture) capture(live);  // before the edit copies the live state
    return edits::editShow(
        live, [&name](Project& p) { return sceneedit::update(p, name); }, "Update Scene " + name);
}

ProjectEdit newScene(Project& live, const std::string& name, const CaptureLive& capture) {
    if (capture) capture(live);
    return edits::editShow(
        live, [&name](Project& p) { return sceneedit::captureNew(p, name); }, "New Scene " + name);
}

}  // namespace spacer::scenecapture
