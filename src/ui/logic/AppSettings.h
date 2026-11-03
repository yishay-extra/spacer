#pragma once

// App-level preferences that are not part of the project (SPEC §14: the
// virtual device latency is "an app setting"). Stored next to the
// installation settings in the user's application-data folder. Plain C++,
// unit-tested on Linux.

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "core/Expected.h"
#include "core/ProjectFile.h"

namespace spacer {

struct AppSettings {
    /// Spacer.driver hop latency in frames (SPEC §14: 128 / 256 / 512).
    int virtualDeviceLatencyFrames{512};
    /// MIDI inputs (by name) that are not opened. New devices are on by default.
    std::vector<std::string> disabledMidiInputs;

    bool midiInputEnabled(const std::string& name) const;
    bool operator==(const AppSettings&) const = default;
};

namespace appsettings {

inline constexpr int kVirtualLatencyChoices[] = {128, 256, 512};
inline constexpr int kDefaultVirtualLatency = 512;

/// The nearest allowed latency (ties go to the smaller one).
int normaliseVirtualLatency(int frames);

/// Enables / disables one MIDI input (keeps the list sorted and unique).
AppSettings withMidiInput(AppSettings settings, const std::string& name, bool enabled);

std::string toJsonText(const AppSettings& settings);
/// Unknown keys are ignored; out-of-range values are normalised.
Expected<AppSettings, Error> parse(const std::string& text);
/// A missing file is not an error for the caller: it gets nullopt-like failure here.
Expected<AppSettings, Error> load(const std::filesystem::path& path);
/// Atomic (write temp + rename).
std::optional<Error> save(const AppSettings& settings, const std::filesystem::path& path);

}  // namespace appsettings

}  // namespace spacer
