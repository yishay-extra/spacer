#pragma once

// Top-level project model (SPEC §4, §15).

#include <string>
#include <vector>

#include "core/Cue.h"
#include "core/Layout.h"
#include "core/Scene.h"
#include "core/Source.h"
#include "core/SpeakerChain.h"

namespace spacer {

inline constexpr int kProjectSchemaVersion = 1;

struct DeviceMapping {
    std::string outputDeviceName;
    double sampleRate{48000.0};
    int bufferSize{128};
    float speedOfSound{343.0f};
    bool operator==(const DeviceMapping&) const = default;
};

struct AmbisonicsConfig {
    int order{3};  // 1..5
    bool operator==(const AmbisonicsConfig&) const = default;
};

struct OscOutConfig {
    std::string host{"127.0.0.1"};
    int port{9001};
    bool enabled{false};
    bool operator==(const OscOutConfig&) const = default;
};
struct OscConfig {
    int inPort{9000};
    OscOutConfig out;
    bool operator==(const OscConfig&) const = default;
};

enum class MidiMessageType { Cc, Cc14, Nrpn, PitchBend };
enum class MidiCurve { Linear, Log };

struct MidiMapping {
    MidiMessageType type{MidiMessageType::Cc};
    int channel{0};  // 0 = any, 1..16
    int number{0};   // CC / NRPN number (unused for PitchBend)
    std::string parameterPath;
    float min{0.0f};
    float max{1.0f};
    MidiCurve curve{MidiCurve::Linear};
    bool operator==(const MidiMapping&) const = default;
};
struct MidiConfig {
    std::vector<MidiMapping> mappings;
    bool operator==(const MidiConfig&) const = default;
};

enum class ClockKind { Internal, Link, Midi, Mtc, Osc };
struct ClockConfig {
    ClockKind kind{ClockKind::Internal};
    double bpm{120.0};
    int beatsPerBar{4};
    bool quantisedStart{false};  // internal clock: start on next bar (SPEC §9)
    bool operator==(const ClockConfig&) const = default;
};

struct Project {
    int schemaVersion{kProjectSchemaVersion};
    std::string name{"Untitled"};
    Layout layout;
    DeviceMapping devices;
    AmbisonicsConfig ambisonics;
    std::vector<SourceSpec> sources;
    std::vector<SpeakerChainSpec> speakerChains;
    MasterSpec master;
    std::vector<Scene> scenes;
    std::vector<CueList> cueLists;
    OscConfig osc;
    MidiConfig midi;
    ClockConfig clock;
    bool operator==(const Project&) const = default;
};

}  // namespace spacer
