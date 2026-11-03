#pragma once

// Sources panel operations (SPEC §16 item 2, §8, §10): the field tables of a
// source strip (input, level, spatializer, distance, modulators), add /
// remove / duplicate, and the ProjectEdit builders that turn a typed value
// into an undoable edit with the right engine effect. Plain C++, unit-tested
// on Linux; the JUCE panel renders the tables and calls the edit builders.
//
// Engine routing of the edits (see ProjectEdit.cpp / EngineSync.cpp):
//   - gain, mute, spread, width, distance, Manual positions: commands,
//   - continuous motion parameters of a moving motion: cmd::SetMotionParam
//     (EngineEffect::MotionParam), the phase is not restarted,
//   - everything else (periods, motion kind, modulators, inputs, spatializer,
//     add / remove): a graph rebuild (SwapGraph, SPEC §5.2).

#include <optional>
#include <string>
#include <vector>

#include "core/Project.h"
#include "ui/logic/InsertEditing.h"
#include "ui/logic/MotionFields.h"
#include "ui/logic/ProjectEdit.h"

namespace spacer::sourceedit {

inline constexpr float kGainMinDb = fmt::kGainFloorDb;
inline constexpr float kGainMaxDb = 12.0f;

// ---- model operations ------------------------------------------------------

/// max(id) + 1, at least 1.
int nextSourceId(const Project& project);
/// `base`, or `base 2`, `base 3` … if taken.
std::string uniqueName(const Project& project, const std::string& base);
/// A new source of `kind` with a fresh id and name: Manual motion at the front
/// (0°, 0°, 2 m); inputs: next unused virtual channel, hardware channel 0, an
/// empty file, a 440 Hz sine.
SourceSpec makeSource(const Project& project, SourceKind kind);
/// "Virtual input", "Hardware input", "File", "Generator".
const char* sourceKindName(SourceKind kind);
/// "VBAP", "DBAP", "Ambisonic", "Direct".
const char* spatializerName(SpatializerKind kind);

/// Changes the input kind, keeping stereo / channels where they carry over.
bool setInputKind(SourceSpec& source, SourceKind kind);
bool setStereo(SourceSpec& source, bool stereo);
bool setLinkedPoints(SourceSpec& source, bool linked);
bool setSpatializer(SourceSpec& source, SpatializerKind kind, const Layout& layout);
/// Adds / removes a Direct target; refuses to remove the last one.
bool setDirectSpeaker(SourceSpec& source, int speakerId, bool on);

/// Number of control points that have their own motion: 2 for a stereo
/// linkedPoints source, else 1.
int motionChannelCount(const SourceSpec& source);
/// The motion of control point `channel` (0 = left / mono, 1 = right).
const MotionSpec& motionOf(const SourceSpec& source, int channel);
MotionSpec& mutableMotionOf(SourceSpec& source, int channel);

bool addModulator(SourceSpec& source);
bool removeModulator(SourceSpec& source, int index);
/// Parameter paths a modulator of this source can target (SPEC §10): gain,
/// spread, width (stereo), distance.* and the live parameters of its motion(s).
std::vector<std::string> modulatorTargets(const SourceSpec& source);

/// One row per insert slot of the strip (SPEC §13): a readable name and the
/// bypass flag (shared with the speaker-chain inspector, see InsertEditing.h).
using InsertRow = insertedit::InsertRow;
std::vector<InsertRow> insertRows(const SourceSpec& source);

// ---- field tables -----------------------------------------------------------

enum class SourceSection { Input, Level, Spatial, Modulators };

/// Fields of one section of the strip, in display order. Ids: `input.*`,
/// `name`, `mute`, `gainDb`, `spatializer`, `spread`, `width`, `linkedPoints`,
/// `stereo`, `direct.<speakerId>`, `distance.k|rMin|q`, `mod.<n>.*`.
std::vector<MotionField> sourceFields(const SourceSpec& source, const Layout& layout, SourceSection section);
/// Applies `text` to a source field (any section). Same text rules as
/// setMotionField(). False (unchanged) for an unknown field or invalid value.
bool setSourceField(SourceSpec& source, const Layout& layout, const std::string& fieldId,
                    const std::string& text);

// ---- undoable edits -----------------------------------------------------------
// The returned edit is invalid (edits::isValid false / nullopt) if the source
// does not exist, the value is invalid, or nothing would change.

/// Appends a new source (invalid when kMaxSources is reached). `newId` receives its id.
ProjectEdit addSource(const Project& project, SourceKind kind, int* newId = nullptr);
/// Inserts a copy right after `sourceId`.
ProjectEdit duplicateSource(const Project& project, int sourceId, int* newId = nullptr);
ProjectEdit removeSource(const Project& project, int sourceId);

/// Edit of one field returned by sourceFields().
std::optional<ProjectEdit> editSourceField(const Project& project, int sourceId, const std::string& fieldId,
                                           const std::string& text);
/// Edit of one field returned by motionFields() for control point `channel`.
/// A continuous parameter of a moving motion becomes EngineEffect::MotionParam.
std::optional<ProjectEdit> editMotionField(const Project& project, int sourceId, int channel,
                                           const std::string& fieldId, const std::string& text);
/// Switches the motion of control point `channel` to `kind` (defaults of that kind).
std::optional<ProjectEdit> editMotionKind(const Project& project, int sourceId, int channel, MotionKind kind);
std::optional<ProjectEdit> editMotionAddItem(const Project& project, int sourceId, int channel);
std::optional<ProjectEdit> editMotionRemoveItem(const Project& project, int sourceId, int channel, int index);
/// Appends an insert running plug-in `pluginId` (<= kMaxInserts, non-empty id): rebuilds.
std::optional<ProjectEdit> editAddInsert(const Project& project, int sourceId, const std::string& pluginId);
std::optional<ProjectEdit> editRemoveInsert(const Project& project, int sourceId, int slot);
/// Bypass on / off: live (cmd::SetInsertBypass, crossfaded), no rebuild.
std::optional<ProjectEdit> editInsertBypass(const Project& project, int sourceId, int slot, bool bypassed);
std::optional<ProjectEdit> editAddModulator(const Project& project, int sourceId);
std::optional<ProjectEdit> editRemoveModulator(const Project& project, int sourceId, int index);

}  // namespace spacer::sourceedit
