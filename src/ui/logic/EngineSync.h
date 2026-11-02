#pragma once

// Turns the engine effect of a model edit into what the app sends to the
// engine: either a graph rebuild (structural, SPEC §5.2) or parameter
// commands derived from the *current* model, so that apply, undo and redo all
// use the same path. Plain C++ (engine/Command.h is JUCE-free), unit-tested
// on Linux.

#include <vector>

#include "core/Project.h"
#include "engine/Command.h"
#include "ui/logic/ProjectEdit.h"

namespace spacer {

struct EngineSyncPlan {
    bool rebuildGraph{false};
    std::vector<Command> commands;
};

/// For Source: gain, mute, spread, width, distance and Manual positions of
/// that source (a rebuild if it is not Manual-only or does not exist). For
/// Speaker: trim, delay, polarity, mute, EQ of the effective chain (SPEC §7).
/// For Master: master gain.
EngineSyncPlan planEngineSync(const Project& project, const EngineEffect& effect);

}  // namespace spacer
