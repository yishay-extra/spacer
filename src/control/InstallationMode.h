#pragma once

// Installation mode (SPEC §11): an app preference, not part of the project.
// On launch: open the last project, arm the default cue list, Go; no dialogs;
// autosave while running. (Relaunch after a crash is a LaunchAgent: WP13.)

#include <filesystem>
#include <functional>
#include <optional>
#include <string>

#include "control/CueEngine.h"
#include "core/Expected.h"
#include "core/ProjectFile.h"

namespace spacer {

struct InstallationSettings {
    bool enabled{false};
    std::string lastProjectPath;
    std::string defaultCueList;  // empty = the project's first list
    int autosaveSeconds{60};
    bool suppressDialogs{true};
    bool operator==(const InstallationSettings&) const = default;
};

namespace installation {

std::string toJsonText(const InstallationSettings& settings);
Expected<InstallationSettings, Error> parseSettings(const std::string& text);
Expected<InstallationSettings, Error> loadSettings(const std::filesystem::path& path);
std::optional<Error> saveSettings(const InstallationSettings& settings, const std::filesystem::path& path);

/// "<dir>/<stem>.autosave.spacer" next to the project.
std::filesystem::path autosavePath(const std::filesystem::path& projectPath);

/// Opens settings.lastProjectPath. If that file is missing or unreadable, falls
/// back to its autosave (a crash can leave the main file half-written by
/// other tools, never by us: saves are atomic). `usedAutosave` tells which.
Expected<LoadedProject, Error> autoload(const InstallationSettings& settings, bool* usedAutosave = nullptr);

/// Arms the default (or first) cue list and fires its first cue. False if
/// there is nothing to arm or fire.
bool startShow(CueEngine& cues, const InstallationSettings& settings, const CueTick& tick);
/// Arms the default cue list (`settings.defaultCueList`), or the first list
/// when that is empty. False if there is no such list. Used by startShow
/// and by Go with nothing armed (WP14).
bool armDefault(CueEngine& cues, const InstallationSettings& settings, const CueTick& tick);

}  // namespace installation

/// Periodic autosave of the live project to installation::autosavePath().
/// Call tick() from the message thread; it writes at most every interval and
/// only when `dirty`.
class Autosaver {
public:
    Autosaver(std::filesystem::path projectPath, double intervalSeconds)
        : path_(installation::autosavePath(projectPath)), interval_(intervalSeconds) {}

    /// Returns true if a file was written. `lastError()` holds the reason otherwise.
    bool tick(double nowSeconds, const Project& project, bool dirty);
    /// As above; `beforeSave` runs only when a file is about to be written and
    /// may update `project` first (the app captures live plugin state, SPEC §13).
    bool tick(double nowSeconds, Project& project, bool dirty,
              const std::function<void(Project&)>& beforeSave);
    const std::filesystem::path& path() const { return path_; }
    const std::string& lastError() const { return lastError_; }

private:
    bool due(double nowSeconds, bool dirty);
    bool write(const Project& project);

    std::filesystem::path path_;
    double interval_;
    double lastSave_{0.0};
    bool started_{false};
    std::string lastError_;
};

}  // namespace spacer
