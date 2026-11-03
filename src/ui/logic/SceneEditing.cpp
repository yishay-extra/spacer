#include "ui/logic/SceneEditing.h"

#include <algorithm>

#include "core/SceneDiff.h"

namespace spacer::sceneedit {

namespace {
std::string trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    return s.substr(b, s.find_last_not_of(" \t\r\n") - b + 1);
}
}  // namespace

int indexOfScene(const Project& project, const std::string& name) {
    for (std::size_t i = 0; i < project.scenes.size(); ++i)
        if (project.scenes[i].name == name) return static_cast<int>(i);
    return -1;
}

std::string uniqueSceneName(const Project& project, const std::string& base) {
    std::string name = base;
    for (int n = 2; indexOfScene(project, name) >= 0; ++n) name = base + " " + std::to_string(n);
    return name;
}

bool captureNew(Project& project, const std::string& name) {
    const std::string n = trim(name);
    if (n.empty() || indexOfScene(project, n) >= 0) return false;
    project.scenes.push_back(captureScene(project, n));
    return true;
}

bool update(Project& project, const std::string& name) {
    const int i = indexOfScene(project, name);
    if (i < 0) return false;
    project.scenes[static_cast<std::size_t>(i)] = captureScene(project, name);
    return true;
}

bool rename(Project& project, const std::string& from, const std::string& to) {
    const std::string n = trim(to);
    const int i = indexOfScene(project, from);
    if (i < 0 || n.empty()) return false;
    const int clash = indexOfScene(project, n);
    if (clash >= 0 && clash != i) return false;
    project.scenes[static_cast<std::size_t>(i)].name = n;
    for (auto& list : project.cueLists)
        for (auto& cue : list.cues)
            if (cue.sceneName == from) cue.sceneName = n;
    return true;
}

bool remove(Project& project, const std::string& name) {
    const int i = indexOfScene(project, name);
    if (i < 0) return false;
    project.scenes.erase(project.scenes.begin() + i);
    return true;
}

std::vector<CueRef> cuesUsing(const Project& project, const std::string& sceneName) {
    std::vector<CueRef> out;
    for (std::size_t l = 0; l < project.cueLists.size(); ++l)
        for (std::size_t c = 0; c < project.cueLists[l].cues.size(); ++c)
            if (project.cueLists[l].cues[c].sceneName == sceneName) out.push_back({l, c});
    return out;
}

LiveStatus liveStatus(const Project& project, const std::string& sceneName) {
    LiveStatus s;
    const int i = indexOfScene(project, sceneName);
    if (i < 0) return s;
    s.known = true;
    s.modified = !(captureScene(project, sceneName) == project.scenes[static_cast<std::size_t>(i)]);
    return s;
}

}  // namespace spacer::sceneedit
