#pragma once

// Settings dialog logic (SPEC §16.5): value choices, text-field parsing and
// validation, which project settings a change touches (device restart, graph
// rebuild, control re-bind), the undoable edit, clock-source options and the
// aggregate-device request. Plain C++, no JUCE; unit-tested on Linux.
//
// Project-stored settings (devices, speed of sound, OSC, MIDI program-change
// channel, clock) change through one undoable edit, SettingsFields. App-only
// settings (virtual device latency, MIDI input selection) are AppSettings.

#include <optional>
#include <string>
#include <vector>

#include "core/Project.h"
#include "ui/logic/ProjectEdit.h"

namespace spacer::settings {

// ---- value choices -------------------------------------------------------------

inline constexpr double kStandardSampleRates[] = {44100.0, 48000.0, 88200.0, 96000.0};  // SPEC §1
inline constexpr int kMinBufferSize = 32;                                               // SPEC §1
inline constexpr int kMaxBufferSize = 2048;
/// UI range for the speed of sound (m/s). The model only requires > 0; this
/// covers about -50..+60 degC.
inline constexpr float kMinSpeedOfSound = 300.0f;
inline constexpr float kMaxSpeedOfSound = 400.0f;
inline constexpr double kMinBpm = 20.0;  // SPEC §9
inline constexpr double kMaxBpm = 300.0;
inline constexpr int kMaxBeatsPerBar = 32;

/// Standard rates the device supports (all of them if `deviceRates` is empty),
/// plus `current` if it is not among them, ascending.
std::vector<double> sampleRateChoices(const std::vector<double>& deviceRates, double current);
/// Buffer sizes within [32, 2048] the device supports (powers of two if
/// `deviceSizes` is empty), plus `current`, ascending.
std::vector<int> bufferSizeChoices(const std::vector<int>& deviceSizes, int current);

/// "48 kHz", "44.1 kHz", "88.2 kHz".
std::string formatSampleRate(double hz);
/// Buffer size and its duration at `sampleRate`: "128 (2.7 ms)".
std::string formatBufferSize(int frames, double sampleRate);

// ---- text field parsing -----------------------------------------------------------

template <typename T>
struct Parsed {
    std::optional<T> value;
    std::string error;  // set when value is empty
    explicit operator bool() const { return value.has_value(); }
};

Parsed<int> parsePort(const std::string& text);  // 1..65535
Parsed<std::string> parseHost(const std::string& text);
Parsed<double> parseBpm(const std::string& text);
Parsed<int> parseBeatsPerBar(const std::string& text);
Parsed<float> parseSpeedOfSound(const std::string& text);

// ---- the edit ----------------------------------------------------------------------

/// The project fields the settings dialog edits.
struct SettingsFields {
    DeviceMapping devices;
    OscConfig osc;
    int programChangeChannel{0};
    ClockConfig clock;
};

SettingsFields fieldsOf(const Project& project);
void apply(Project& project, const SettingsFields& fields);

/// What a change from `before` to `after` requires of the app.
struct Delta {
    bool device{false};  // output device, sample rate or buffer size: reopen the device
    bool graph{false};   // speed of sound or clock config: rebuild the graph (ClockSelector switches)
    bool osc{false};     // OSC in / out: re-bind the ports
    bool midi{false};    // MIDI program-change channel: reload the mapper
    bool any() const { return device || graph || osc || midi; }
};

Delta classify(const SettingsFields& before, const SettingsFields& after);

/// Why `fields` cannot be applied ("" if fine): bufferSize outside [32, 2048],
/// rate <= 0, speed of sound outside the UI range, ports, empty OSC host,
/// bpm / beatsPerBar, program-change channel outside 0..16.
std::string validate(const SettingsFields& fields);

}  // namespace spacer::settings

namespace spacer::edits {

/// Sets the settings fields; undo restores the previous ones. The engine
/// effect is Structural when the speed of sound or the clock config changes
/// (device / OSC / MIDI changes are applied by the app from the model diff).
/// Edits with the same non-empty `mergeKey` merge inside a gesture.
ProjectEdit setSettings(const Project& project, settings::SettingsFields after,
                        std::string name = "Change Settings", std::string mergeKey = {});

}  // namespace spacer::edits

namespace spacer::settings {

// ---- clock source ------------------------------------------------------------------

struct ClockOption {
    ClockKind kind;
    std::string label;
};

/// Internal, MIDI clock, MTC, OSC, and Link only when the app was built with it.
std::vector<ClockOption> clockOptions(bool linkAvailable);
std::string clockKindName(ClockKind kind);
/// What the source needs: "Follows MIDI beat clock from any MIDI input." etc.
std::string clockKindHint(ClockKind kind);
/// "Locked" / "Searching..." / "Internal"; `active` is the kind that really runs.
std::string clockLockText(ClockKind configured, ClockKind active, bool locked);

// ---- aggregate device --------------------------------------------------------------

struct AudioDeviceInfo {
    std::string uid;
    std::string name;
    int numInputs{0};
    int numOutputs{0};
};

/// Devices that make sense as aggregate members: outputs > 0, and not the
/// Spacer virtual device itself (it is a sender-side device, SPEC §14).
std::vector<AudioDeviceInfo> aggregateCandidates(const std::vector<AudioDeviceInfo>& all);

struct AggregateRequest {
    std::string name;
    std::string uid;
    std::vector<std::string> subDeviceUids;  // in channel order; the first is the clock
    std::string error;                       // non-empty: not creatable
};

/// "org.spacer.aggregate.<name with non-alphanumerics replaced by '-'>".
std::string aggregateUid(const std::string& name);
/// Validates name (non-empty, not the name of an existing device) and members
/// (>= 2 distinct, all known candidates).
AggregateRequest planAggregate(const std::string& name, const std::vector<std::string>& chosenUids,
                               const std::vector<AudioDeviceInfo>& all);

}  // namespace spacer::settings
