#pragma once

// Control-path strings: the parameter a MIDI mapping targets (SPEC §12.2,
// "same paths as modulators"). A path is a core ParameterPath, optionally
// scoped to an entity:
//
//   source.<id>.<ParameterPath>   e.g. source.3.gain, source.3.motion.radius
//   <ParameterPath>               the UI-selected source (resolved by the dispatcher)
//   speaker.<id>.trim | speaker.<id>.mute
//   source.<id>.insert.<slot>.param.<index>    plugin parameter (SPEC §13), 0..1
//   speaker.<id>.insert.<slot>.param.<index>
//   insert.<slot>.param.<index>                 the UI-selected source's plugin
//   master.gain
//   clock.bpm
//
// ParameterPath itself is never re-parsed here: the remainder after the
// scope is handed to it unchanged by the dispatcher.

#include <optional>
#include <string_view>

#include "control/ControlEvent.h"

namespace spacer::controlpath {

/// True if `path` names a settable control target.
bool isValid(std::string_view path);

/// The event that sets `path` to `value` (in the parameter's own unit). Mute
/// targets treat value >= 0.5 as true. nullopt for an invalid path.
std::optional<ControlEvent> makeEvent(std::string_view path, float value);

}  // namespace spacer::controlpath
