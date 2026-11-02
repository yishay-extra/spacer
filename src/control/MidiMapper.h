#pragma once

// MIDI mapping (SPEC §12.2): turns incoming MIDI messages into ControlEvents
// via the project's MidiConfig. Pure logic over juce::MidiMessage, no device
// I/O, so it is testable with synthetic messages. Called on the message
// thread only (the device callback hands messages over first).

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "control/ControlEvent.h"
#include "core/Project.h"

namespace spacer {

class MidiMapper {
public:
    struct LearnDefaults {
        float min{0.0f};
        float max{1.0f};
        MidiCurve curve{MidiCurve::Linear};
    };
    struct Stats {
        std::uint64_t messages{0};
        std::uint64_t unmapped{0};     // controller / pitch bend with no mapping
        std::uint64_t invalidPath{0};  // a mapping's parameterPath is not a control path
    };

    /// Replaces mappings and programChangeChannel; clears 14-bit / NRPN state.
    void setConfig(const MidiConfig& config);
    /// Current config, including mappings created by MIDI learn: the app
    /// writes it back to the project.
    const MidiConfig& config() const noexcept { return config_; }

    /// The next CC / NRPN / pitch-bend message that arrives is captured as a
    /// mapping for `parameterPath` (channel = the message's channel). An
    /// existing mapping for the same path is replaced. Notes and program
    /// changes are not learnable controls. `onLearned` is invoked from handle().
    void startLearn(std::string parameterPath, LearnDefaults defaults);
    void startLearn(std::string parameterPath) { startLearn(std::move(parameterPath), LearnDefaults{}); }
    void cancelLearn() noexcept { learn_.reset(); }
    bool learning() const noexcept { return learn_.has_value(); }
    std::function<void(const MidiMapping&)> onLearned;

    /// Appends the events `message` produces to `out`.
    void handle(const juce::MidiMessage& message, std::vector<ControlEvent>& out);

    Stats stats() const noexcept { return stats_; }

private:
    struct Nrpn {
        int msb{-1}, lsb{-1};
        bool selected{false};
        int dataMsb{0}, dataLsb{0};
    };
    struct ChannelState {
        std::array<int, 32> msb{}, lsb{};
        Nrpn nrpn;
    };
    struct Learn {
        std::string path;
        LearnDefaults defaults;
    };

    void handleController(int channel, int number, int value, std::vector<ControlEvent>& out);
    bool control(MidiMessageType type, int channel, int number, float normalised,
                 std::vector<ControlEvent>& out);
    void emit(const MidiMapping& mapping, float normalised, std::vector<ControlEvent>& out);
    bool capture(MidiMessageType type, int channel, int number);

    MidiConfig config_;
    std::array<ChannelState, 16> channels_{};
    std::optional<Learn> learn_;
    Stats stats_;
};

}  // namespace spacer
