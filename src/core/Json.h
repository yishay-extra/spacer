#pragma once

// nlohmann/json (de)serialisation for every model type (SPEC §15).
//
// to_json/from_json live in namespace spacer so ADL finds them. from_json is
// lenient about *missing* optional keys (defaults apply) and strict about
// wrong types, unknown enum strings and missing discriminators/ids: those
// throw std::runtime_error (or nlohmann::json::exception) with a message that
// starts with the key path. ProjectFile turns those into Errors.

#include <nlohmann/json.hpp>

#include "core/Cue.h"
#include "core/Layout.h"
#include "core/MotionSpec.h"
#include "core/PluginSlot.h"
#include "core/Position.h"
#include "core/Project.h"
#include "core/Scene.h"
#include "core/Source.h"
#include "core/SpeakerChain.h"

namespace spacer {

using Json = nlohmann::json;

#define SPACER_JSON_DECL(T)            \
    void to_json(Json& j, const T& v); \
    void from_json(const Json& j, T& v)

// Geometry / layout
SPACER_JSON_DECL(Cartesian);
SPACER_JSON_DECL(Position);
SPACER_JSON_DECL(Speaker);
SPACER_JSON_DECL(Layout);

// Motion
SPACER_JSON_DECL(Period);
SPACER_JSON_DECL(ManualMotion);
SPACER_JSON_DECL(OrbitMotion);
SPACER_JSON_DECL(LineMotion);
SPACER_JSON_DECL(LissajousMotion);
SPACER_JSON_DECL(PathMotion);
SPACER_JSON_DECL(RandomWalkMotion);
SPACER_JSON_DECL(Step);
SPACER_JSON_DECL(StepSequenceMotion);
SPACER_JSON_DECL(MotionSpec);  // adds/reads "kind"
SPACER_JSON_DECL(ModulatorSpec);

// Sources / chains / scenes
SPACER_JSON_DECL(PluginSlot);
SPACER_JSON_DECL(DistanceModel);
SPACER_JSON_DECL(SourceSpec);  // writes "kind" + "input"
SPACER_JSON_DECL(Eq3);
SPACER_JSON_DECL(SpeakerChainSpec);
SPACER_JSON_DECL(MasterSpec);
SPACER_JSON_DECL(Scene);

// Cues
SPACER_JSON_DECL(Trigger);  // adds/reads "kind"
SPACER_JSON_DECL(Cue);
SPACER_JSON_DECL(CueList);

// Project
SPACER_JSON_DECL(DeviceMapping);
SPACER_JSON_DECL(AmbisonicsConfig);
SPACER_JSON_DECL(OscOutConfig);
SPACER_JSON_DECL(OscConfig);
SPACER_JSON_DECL(MidiMapping);
SPACER_JSON_DECL(MidiConfig);
SPACER_JSON_DECL(ClockConfig);
SPACER_JSON_DECL(Project);

#undef SPACER_JSON_DECL

/// The body of SourceSpec's "input" object (without the "kind" discriminator).
Json inputToJson(const InputSpec& input);
InputSpec inputFromJson(SourceKind kind, const Json& j);

/// Paths of keys present in `input` but not in `canonical` (typically the
/// re-serialisation of the parsed model), e.g. "sources[0].foo".
std::vector<std::string> findUnknownKeys(const Json& input, const Json& canonical);

}  // namespace spacer
