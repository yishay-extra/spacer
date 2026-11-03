#pragma once

// Scene recall planning (SPEC §11). Pure data and maths: given the scene the
// engine is in and the scene to recall, work out
//   - which continuous parameters ramp, and to what,
//   - which sources must be added, removed or replaced (discrete changes:
//     spatializer kind, motion kind, inserts, input, ... switch at the start of
//     the transition with a 50 ms crossfade).
// The engine (src/engine) applies the result; this file knows nothing about it.

#include <string>
#include <vector>

#include "core/Layout.h"
#include "core/Position.h"
#include "core/Project.h"
#include "core/Scene.h"

namespace spacer {

/// Parameters that move from their live value to a target over the transition.
/// Boolean targets (mute, polarity) carry 0 / 1.
enum class RampTarget {
    SourceGainDb,
    SourceMute,
    SourceSpread,
    SourceWidth,
    SourceDistanceK,
    SourceDistanceRMin,
    SourceDistanceQ,
    SourcePosition,  // ManualMotion position; `channel` -1 = control point, 0 / 1 = linkedPoints L / R
    SpeakerTrimDb,
    SpeakerMute,
    SpeakerUserDelayMs,  // the engine crossfades delay changes itself (SPEC §5.4)
    SpeakerPolarity,     // the engine ramps through zero itself
    SpeakerEqLowFreq,
    SpeakerEqLowGainDb,
    SpeakerEqMidFreq,
    SpeakerEqMidGainDb,
    SpeakerEqMidQ,
    SpeakerEqHighFreq,
    SpeakerEqHighGainDb,
    MasterGainDb,
    // Insert slots (SPEC §13), `slot` = index into the inserts list.
    SourceInsertParam,   // `param` = plugin parameter index, normalised value
    SourceInsertBypass,  // discrete: the engine crossfades the bypass itself
    SpeakerInsertParam,
    SpeakerInsertBypass,
    // A continuous parameter of a running (non-Manual) motion of the same
    // kind (WP14): `param` = MotionParam, `channel` as SourcePosition. The
    // engine applies it with SetMotionParam semantics (phase kept).
    SourceMotionParam,
};

struct Ramp {
    RampTarget target{RampTarget::SourceGainDb};
    int id{0};        // source id or speaker id (unused for the master)
    int channel{-1};  // SourcePosition / SourceMotionParam only
    int slot{-1};     // insert targets only
    int param{-1};    // *InsertParam: plugin parameter index; SourceMotionParam: MotionParam
    float from{0.0f};
    float to{0.0f};
    Position fromPosition;  // SourcePosition only
    Position toPosition;
};

struct SceneDiff {
    std::vector<Ramp> ramps;
    /// Source ids (always ids of the *target* scene except `removedSources`).
    std::vector<int> addedSources;
    std::vector<int> removedSources;
    /// Present in both but changed in a way that cannot be ramped: the old
    /// source crossfades to a new one built from the target.
    std::vector<int> replacedSources;
    /// Present in both (not replaced) but the insert slots differ in structure
    /// (see sameInsertStructure): the source keeps playing and its insert chain
    /// crossfades from the old plugins to the new ones (SPEC §11, §13).
    std::vector<int> sourceInsertChanges;
    /// Speakers whose insert slots differ in structure (same crossfade).
    std::vector<int> speakerInsertChanges;

    /// True when a new graph is needed.
    bool structural() const {
        return !addedSources.empty() || !removedSources.empty() || !replacedSources.empty() ||
               !sourceInsertChanges.empty() || !speakerInsertChanges.empty();
    }
    bool empty() const { return ramps.empty() && !structural(); }
};

/// Two insert lists have the same structure when they have the same length
/// and every slot has the same plugin and state blob: then only parameter
/// values and bypass flags differ, which ramp / crossfade in place. Anything
/// else (a slot added, removed, reordered, another plugin or another state)
/// needs new plugin instances.
bool sameInsertStructure(const std::vector<PluginSlot>& a, const std::vector<PluginSlot>& b);

/// Effective per-speaker state of a scene: the scene's chain spec if it has
/// one, else the layout speaker's own defaults (matches GraphBuilder).
SpeakerChainSpec effectiveChain(const Scene& scene, const Speaker& speaker);

/// What changes when the engine goes from `current` to `target`.
SceneDiff diffScenes(const Scene& current, const Scene& target, const Layout& layout);

/// The scene a new graph is built from for a structural recall: the target's
/// structure (sources present, discrete settings, inserts) with the
/// *continuous* values of every source / speaker that persists taken from
/// `current`, so that the ramps in `diff` start where the audio already is.
/// Added and replaced sources are taken whole from `target`.
Scene intermediateScene(const Scene& current, const Scene& target, const SceneDiff& diff,
                        const Layout& layout);

// ---- Project <-> scene -----------------------------------------------------

/// Snapshot of the project's live state (sources, speaker chains, master).
Scene captureScene(const Project& project, std::string name);
/// Replace the project's live state with the scene's. Layout, devices, cue
/// lists and the scene list are untouched.
void applyScene(Project& project, const Scene& scene);
/// Null if there is no scene of that name.
const Scene* findScene(const Project& project, const std::string& name);

}  // namespace spacer
