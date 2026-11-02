#pragma once

// Built-in layout presets (SPEC §7). The JSON files in resources/layouts/ are
// embedded at configure time; no files are read at runtime.

#include <optional>
#include <string>
#include <vector>

#include "core/Layout.h"

namespace spacer::layoutpresets {

/// Preset ids (file stems): "stereo", "quad", "5.1", "7.1", "7.1.4",
/// "octagon", "ring12", "ring16", "dome22".
std::vector<std::string> ids();

/// Parse the preset with this id; nullopt if unknown.
std::optional<Layout> load(const std::string& id);

}  // namespace spacer::layoutpresets
