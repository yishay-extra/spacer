#pragma once

// Layout editor operations (SPEC §16 item 4, §7): add / remove / duplicate /
// move speakers, edit table fields, presets, output-channel assignment and
// the read-only compensation display. Plain C++, unit-tested on Linux. The
// functions edit a Layout value; undo is ProjectHistory's job.

#include <optional>
#include <string>
#include <vector>

#include "core/Project.h"

namespace spacer::layoutedit {

/// Speakers are never placed closer than this (SPEC §7: distance > 0).
inline constexpr float kMinSpeakerDistance = 0.1f;

/// max(id) + 1, at least 1.
int nextSpeakerId(const Layout& layout);
/// Lowest device channel (0-based) no speaker uses.
int nextFreeOutputChannel(const Layout& layout);

/// Appends a speaker at `position` with a fresh id, name = id and the next
/// free output channel. nullopt (layout unchanged) if the layout is full
/// (kMaxSpeakers).
std::optional<int> addSpeaker(Layout& layout, const Position& position);
/// Removes the speaker. False if it does not exist or the layout would drop
/// below 2 speakers (SPEC §7 minimum).
bool removeSpeaker(Layout& layout, int speakerId);
/// Copies the speaker (new id, next free channel, name + " copy") right after
/// the original. nullopt if not found or the layout is full.
std::optional<int> duplicateSpeaker(Layout& layout, int speakerId);
/// Sets the speaker's position (normalised, distance >= kMinSpeakerDistance).
bool moveSpeaker(Layout& layout, int speakerId, const Position& position);
/// Output channels 0, 1, 2 … in layout order.
void assignOutputsSequentially(Layout& layout);
/// The built-in preset (SPEC §7), or nullopt for an unknown id.
std::optional<Layout> preset(const std::string& presetId);

/// Editable columns of the speaker table.
enum class SpeakerField { Name, Azimuth, Elevation, Distance, TrimDb, DelayMs, Polarity, OutputChannel, Lfe };

/// Text shown in the table. Output channels are shown 1-based ("" when unassigned).
std::string formatField(const Speaker& speaker, SpeakerField field);
/// Parses `text` into the field. False (speaker unchanged) if the text is not
/// a valid value. Output channels are typed 1-based; "", "-" or "0" = unassigned.
/// Booleans accept 1/0, true/false, yes/no, on/off, x.
bool setField(Speaker& speaker, SpeakerField field, const std::string& text);

/// Edits one field of speaker `speakerId` in `layout`. True only if the text
/// was valid and the speaker actually changed (so no empty undo steps).
bool editSpeakerField(Layout& layout, int speakerId, SpeakerField field, const std::string& text);

/// One row of the read-only compensation display (SPEC §7).
struct CompensationRow {
    int speakerId{0};
    float compensationDelayMs{0.0f};
    float compensationGainDb{0.0f};
    float userDelayMs{0.0f};  // effective: from the speaker chain if there is one
    float trimDb{0.0f};       // effective: from the speaker chain if there is one
    bool overriddenByChain{false};
    float effectiveDelayMs() const { return compensationDelayMs + userDelayMs; }
    float effectiveGainDb() const { return compensationGainDb + trimDb; }
};

/// Rows in layout order, with `project.devices.speedOfSound`.
std::vector<CompensationRow> compensationRows(const Project& project);

/// What the speaker table shows in a cell (SPEC §7 override made visible):
/// for trim, delay and polarity of a speaker whose speaker chain overrides
/// the layout, the chain's (effective) value, marked as coming from the
/// chain; the layout value it hides is in the tooltip. The cell is then not
/// editable from the table (the layout value would have no effect); the
/// chain is edited in the speaker inspector / by scenes.
struct TableCell {
    std::string text;
    bool fromChain{false};
    bool editable{true};
    std::string tooltip;
};
TableCell tableCell(const Project& project, const Speaker& speaker, SpeakerField field);

}  // namespace spacer::layoutedit
