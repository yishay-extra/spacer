#include "ui/logic/CueListEditing.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "ui/logic/TriggerText.h"

namespace spacer::cueedit {

namespace {

std::string trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    return s.substr(b, s.find_last_not_of(" \t\r\n") - b + 1);
}

bool parseSeconds(const std::string& text, float maxValue, float& out) {
    const std::string t = trim(text);
    if (t.empty()) return false;
    char* end = nullptr;
    const double v = std::strtod(t.c_str(), &end);
    std::string rest = trim(end);
    if (rest == "s" || rest == "sec") rest.clear();
    if (!rest.empty() || !std::isfinite(v) || v < 0.0 || v > static_cast<double>(maxValue)) return false;
    out = static_cast<float>(v);
    return true;
}

std::string formatSeconds(float v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.3f", static_cast<double>(v));
    std::string s = buf;
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    return s;
}

bool parseBool(const std::string& text, bool& out) {
    std::string t = trim(text);
    for (auto& c : t) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (t == "1" || t == "true" || t == "yes" || t == "on" || t == "x") return out = true, true;
    if (t == "0" || t == "false" || t == "no" || t == "off" || t.empty() || t == "-")
        return out = false, true;
    return false;
}

bool inRange(const CueList& list, int index) {
    return index >= 0 && index < static_cast<int>(list.cues.size());
}

}  // namespace

Cue makeCue(const Project& project, const CueList& list, const std::string& sceneName) {
    Cue c;
    c.name = "Cue " + std::to_string(list.cues.size() + 1);
    c.sceneName = !sceneName.empty() ? sceneName
                                     : (project.scenes.empty() ? std::string{} : project.scenes.front().name);
    return c;
}

std::optional<int> addCue(CueList& list, Cue cue, int afterIndex) {
    if (list.cues.size() >= kMaxCuesPerList) return std::nullopt;
    const int at = std::clamp(afterIndex + 1, 0, static_cast<int>(list.cues.size()));
    list.cues.insert(list.cues.begin() + at, std::move(cue));
    return at;
}

bool removeCue(CueList& list, int index) {
    if (!inRange(list, index)) return false;
    list.cues.erase(list.cues.begin() + index);
    return true;
}

std::optional<int> duplicateCue(CueList& list, int index) {
    if (!inRange(list, index)) return std::nullopt;
    Cue copy = list.cues[static_cast<std::size_t>(index)];
    copy.name += " copy";
    return addCue(list, std::move(copy), index);
}

bool moveCue(CueList& list, int from, int to) {
    if (!inRange(list, from) || !inRange(list, to) || from == to) return false;
    Cue c = std::move(list.cues[static_cast<std::size_t>(from)]);
    list.cues.erase(list.cues.begin() + from);
    list.cues.insert(list.cues.begin() + to, std::move(c));
    return true;
}

std::string formatField(const Cue& cue, CueField field) {
    switch (field) {
        case CueField::Name:
            return cue.name;
        case CueField::Scene:
            return cue.sceneName;
        case CueField::Transition:
            return formatSeconds(cue.transitionSeconds);
        case CueField::Trigger:
            return triggertext::format(cue.trigger);
        case CueField::RestartPhase:
            return cue.restartMotionPhase ? "yes" : "no";
        case CueField::PostWait:
            return formatSeconds(cue.postWaitSeconds);
    }
    return {};
}

bool setField(Cue& cue, CueField field, const std::string& text) {
    switch (field) {
        case CueField::Name: {
            const std::string t = trim(text);
            if (t.empty()) return false;
            cue.name = t;
            return true;
        }
        case CueField::Scene: {
            const std::string t = trim(text);
            if (t.empty()) return false;
            cue.sceneName = t;
            return true;
        }
        case CueField::Transition:
            return parseSeconds(text, kMaxTransitionSeconds, cue.transitionSeconds);
        case CueField::Trigger: {
            const auto t = triggertext::parse(text);
            if (!t) return false;
            cue.trigger = *t;
            return true;
        }
        case CueField::RestartPhase:
            return parseBool(text, cue.restartMotionPhase);
        case CueField::PostWait:
            return parseSeconds(text, kMaxPostWaitSeconds, cue.postWaitSeconds);
    }
    return false;
}

bool editCueField(CueList& list, int index, CueField field, const std::string& text) {
    if (!inRange(list, index)) return false;
    Cue& cue = list.cues[static_cast<std::size_t>(index)];
    Cue edited = cue;
    if (!setField(edited, field, text) || edited == cue) return false;
    cue = std::move(edited);
    return true;
}

// ---- cue lists -----------------------------------------------------------------

int indexOfCueList(const Project& project, const std::string& name) {
    for (std::size_t i = 0; i < project.cueLists.size(); ++i)
        if (project.cueLists[i].name == name) return static_cast<int>(i);
    return -1;
}

std::string uniqueCueListName(const Project& project, const std::string& base) {
    std::string name = base;
    for (int n = 2; indexOfCueList(project, name) >= 0; ++n) name = base + " " + std::to_string(n);
    return name;
}

std::optional<int> addCueList(Project& project, const std::string& name) {
    const std::string n = trim(name);
    if (n.empty() || indexOfCueList(project, n) >= 0) return std::nullopt;
    CueList list;
    list.name = n;
    project.cueLists.push_back(std::move(list));
    return static_cast<int>(project.cueLists.size()) - 1;
}

bool renameCueList(Project& project, int index, const std::string& name) {
    if (index < 0 || index >= static_cast<int>(project.cueLists.size())) return false;
    const std::string n = trim(name);
    const int existing = indexOfCueList(project, n);
    if (n.empty() || (existing >= 0 && existing != index)) return false;
    project.cueLists[static_cast<std::size_t>(index)].name = n;
    return true;
}

bool removeCueList(Project& project, int index) {
    if (index < 0 || index >= static_cast<int>(project.cueLists.size())) return false;
    project.cueLists.erase(project.cueLists.begin() + index);
    return true;
}

std::optional<int> duplicateCueList(Project& project, int index) {
    if (index < 0 || index >= static_cast<int>(project.cueLists.size())) return std::nullopt;
    CueList copy = project.cueLists[static_cast<std::size_t>(index)];
    copy.name = uniqueCueListName(project, copy.name + " copy");
    project.cueLists.push_back(std::move(copy));
    return static_cast<int>(project.cueLists.size()) - 1;
}

}  // namespace spacer::cueedit
