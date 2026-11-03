#pragma once

// Insert slots as the UI shows them (SPEC §13), shared by the source strip and
// the speaker-chain inspector: one row per slot with a readable name and the
// bypass flag. Plain C++, unit-tested on Linux.

#include <string>
#include <vector>

#include "core/InsertParamPath.h"
#include "core/PluginSlot.h"
#include "core/Project.h"

namespace spacer::insertedit {

struct InsertRow {
    std::string name;  // last path component / identifier of the plug-in, or "(empty)"
    bool bypassed{false};
};

/// "AudioUnit:Effects/aufx,dely,appl" -> "aufx,dely,appl"; "" -> "(empty)".
/// The UI shows the scanned plug-in's own name when it has one.
std::string fallbackName(const std::string& pluginId);
std::vector<InsertRow> rows(const std::vector<PluginSlot>& slots);

/// The slots of a source or of a speaker's chain; null if there is no such
/// owner (a speaker without a chain has no inserts: null as well).
const std::vector<PluginSlot>* slotsOf(const Project& project, InsertOwner owner, int ownerId);

}  // namespace spacer::insertedit
