#include "control/InstallationMode.h"

#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>

namespace spacer {
namespace installation {

std::string toJsonText(const InstallationSettings& s) {
    nlohmann::json j;
    j["enabled"] = s.enabled;
    j["lastProjectPath"] = s.lastProjectPath;
    j["defaultCueList"] = s.defaultCueList;
    j["autosaveSeconds"] = s.autosaveSeconds;
    j["suppressDialogs"] = s.suppressDialogs;
    return j.dump(2);
}

Expected<InstallationSettings, Error> parseSettings(const std::string& text) {
    const auto j = nlohmann::json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object())
        return Unexpected(Error{"installation settings: not a JSON object"});
    InstallationSettings s;
    try {
        s.enabled = j.value("enabled", s.enabled);
        s.lastProjectPath = j.value("lastProjectPath", s.lastProjectPath);
        s.defaultCueList = j.value("defaultCueList", s.defaultCueList);
        s.autosaveSeconds = j.value("autosaveSeconds", s.autosaveSeconds);
        s.suppressDialogs = j.value("suppressDialogs", s.suppressDialogs);
    } catch (const nlohmann::json::exception& e) {
        return Unexpected(Error{std::string("installation settings: ") + e.what()});
    }
    return s;
}

Expected<InstallationSettings, Error> loadSettings(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return Unexpected(Error{"cannot open " + path.string()});
    std::stringstream ss;
    ss << in.rdbuf();
    return parseSettings(ss.str());
}

std::optional<Error> saveSettings(const InstallationSettings& settings, const std::filesystem::path& path) {
    const auto tmp = path.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return Error{"cannot write " + tmp};
        out << toJsonText(settings);
        if (!out) return Error{"write failed: " + tmp};
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) return Error{"cannot rename " + tmp + ": " + ec.message()};
    return std::nullopt;
}

std::filesystem::path autosavePath(const std::filesystem::path& projectPath) {
    auto p = projectPath;
    p.replace_filename(projectPath.stem().string() + ".autosave.spacer");
    return p;
}

Expected<LoadedProject, Error> autoload(const InstallationSettings& settings, bool* usedAutosave) {
    if (usedAutosave) *usedAutosave = false;
    if (settings.lastProjectPath.empty()) return Unexpected(Error{"no last project"});
    const std::filesystem::path path = settings.lastProjectPath;
    auto main = projectfile::load(path);
    if (main) return main;
    auto backup = projectfile::load(autosavePath(path));
    if (backup) {
        if (usedAutosave) *usedAutosave = true;
        return backup;
    }
    return main;  // report the original failure
}

bool startShow(CueEngine& cues, const InstallationSettings& settings, const CueTick& tick) {
    const bool armed =
        settings.defaultCueList.empty() ? cues.armFirst(tick) : cues.arm(settings.defaultCueList, tick);
    return armed && cues.go(tick);
}

}  // namespace installation

bool Autosaver::tick(double nowSeconds, const Project& project, bool dirty) {
    if (!started_) {
        started_ = true;
        lastSave_ = nowSeconds;
    }
    if (!dirty || nowSeconds - lastSave_ < interval_) return false;
    lastSave_ = nowSeconds;
    if (const auto err = projectfile::save(project, path_)) {
        lastError_ = err->message;
        return false;
    }
    lastError_.clear();
    return true;
}

}  // namespace spacer
