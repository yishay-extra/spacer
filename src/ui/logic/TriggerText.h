#pragma once

// Cue trigger editing, formatting and parsing (SPEC §11, §16 item 3). The
// panel shows a trigger as text and edits it as text:
//
//   manual                         Go
//   bar 5 beat 1                   AtBeat (1-based; "beat 1" may be omitted; beat may be fractional)
//   after 4 s | after 2 beats      AfterPrevious; optional random extra: "after 4 s +2"
//   osc /cue/intro                 Osc
//   note 60 | cc 7 ch2             Midi (channel omitted = any)
//   09:30 | 09:30 mon,wed | 22:00 mon-fri     TimeOfDay (no days = every day)
//
// Parsing is case-insensitive and forgiving about spacing ("after 4s"); an
// invalid text yields nullopt. format(parse(x)) is a fixed point. Plain C++.

#include <optional>
#include <string>

#include "core/Cue.h"

namespace spacer::triggertext {

enum class Kind { Manual, AtBeat, AfterPrevious, Osc, Midi, TimeOfDay };

inline constexpr int kMaxBar = 99999;
inline constexpr double kMaxBeat = 64.0;  // within a bar
inline constexpr double kMaxDelaySeconds = 86400.0;
inline constexpr double kMaxDelayBeats = 4096.0;

Kind kindOf(const Trigger& trigger);
const char* kindName(Kind kind);
/// A valid trigger of that kind with sensible defaults (for a "kind" selector).
Trigger defaultTrigger(Kind kind);

std::string format(const Trigger& trigger);
std::optional<Trigger> parse(const std::string& text);

/// Short day names, 0 = Sunday.
const char* dayName(int day);

}  // namespace spacer::triggertext
