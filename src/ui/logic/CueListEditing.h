#pragma once

// Cue list editing for the Cues panel (SPEC §11, §16 item 3): add / remove /
// duplicate / reorder cues, edit their fields as text, and manage the
// project's cue lists. Plain C++, unit-tested on Linux. The functions edit
// values; undo is ProjectHistory's job (edits::editShow wraps them).

#include <optional>
#include <string>

#include "core/Project.h"

namespace spacer::cueedit {

inline constexpr float kMaxTransitionSeconds = 3600.0f;
inline constexpr float kMaxPostWaitSeconds = 86400.0f;
inline constexpr std::size_t kMaxCuesPerList = 999;

// ---- cues -------------------------------------------------------------------

/// "Cue N" (N = cues + 1), first scene of the project if `sceneName` is empty,
/// Manual trigger, 0 s transition.
Cue makeCue(const Project& project, const CueList& list, const std::string& sceneName = {});

/// Inserts after `afterIndex` (-1 = at the start; >= size = at the end).
/// Returns the new cue's index, nullopt if the list is full.
std::optional<int> addCue(CueList& list, Cue cue, int afterIndex);
bool removeCue(CueList& list, int index);
/// Copy with name + " copy" right after the original.
std::optional<int> duplicateCue(CueList& list, int index);
/// Moves the cue so that it ends up at index `to`. False if out of range or a no-op.
bool moveCue(CueList& list, int from, int to);

/// Editable columns of the cue table.
enum class CueField { Name, Scene, Transition, Trigger, RestartPhase, PostWait };

std::string formatField(const Cue& cue, CueField field);
/// Parses `text` into the field; false (cue unchanged) if invalid. Names must
/// not be empty; times are seconds in [0, max]; booleans accept 1/0,
/// true/false, yes/no, on/off, x. The scene is not checked against the project.
bool setField(Cue& cue, CueField field, const std::string& text);
/// True only if the text was valid and the cue actually changed.
bool editCueField(CueList& list, int index, CueField field, const std::string& text);

// ---- cue lists ---------------------------------------------------------------

/// Index of the list called `name`, -1 if none.
int indexOfCueList(const Project& project, const std::string& name);
/// `base`, or `base 2`, `base 3` ... not used by another list.
std::string uniqueCueListName(const Project& project, const std::string& base);
/// Appends an empty list. nullopt if the name is empty or taken.
std::optional<int> addCueList(Project& project, const std::string& name);
bool renameCueList(Project& project, int index, const std::string& name);
bool removeCueList(Project& project, int index);
/// Copy with a unique "<name> copy" name appended.
std::optional<int> duplicateCueList(Project& project, int index);

}  // namespace spacer::cueedit
