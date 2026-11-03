#pragma once

// Insert slots of a speaker's chain (SPEC §13): add / remove / bypass as
// undoable edits, mirroring SourceEditing's insert edits. A speaker has a
// SpeakerChainSpec in the project only once something per-speaker is stored
// there; it overrides the layout's trim / delay / polarity (SPEC §7), so a
// chain created for an insert starts from the speaker's current values and
// is dropped again when its last insert goes and it holds nothing else.
// Plain C++, unit-tested on Linux.

#include <optional>
#include <string>
#include <vector>

#include "core/Project.h"
#include "ui/logic/InsertEditing.h"
#include "ui/logic/ProjectEdit.h"

namespace spacer::chainedit {

/// The chain of speaker `speakerId`, or null.
const SpeakerChainSpec* findChain(const Project& project, int speakerId);
/// Rows of the speaker's insert slots (empty if none).
std::vector<insertedit::InsertRow> insertRows(const Project& project, int speakerId);

/// True if `chain` only mirrors the speaker's layout values and holds nothing
/// else, so removing it changes nothing audible.
bool isDefaultChain(const SpeakerChainSpec& chain, const Speaker& speaker);

/// Appends an insert running plug-in `pluginId` (<= kMaxInserts, non-empty id,
/// existing speaker): rebuilds the graph.
std::optional<ProjectEdit> editAddInsert(const Project& project, int speakerId, const std::string& pluginId);
std::optional<ProjectEdit> editRemoveInsert(const Project& project, int speakerId, int slot);
/// Bypass on / off: live (cmd::SetInsertBypass), no rebuild.
std::optional<ProjectEdit> editInsertBypass(const Project& project, int speakerId, int slot, bool bypassed);

}  // namespace spacer::chainedit
