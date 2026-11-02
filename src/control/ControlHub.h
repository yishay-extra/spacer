#pragma once

// One integration point for the app (composition root): owns the OSC server /
// sender, the MIDI mapper and the dispatcher, and drains them from the message
// thread. MIDI device callbacks call submitMidi() from their own thread; every
// engine command is pushed from pump(), which must run on the message thread
// (SPEC §5.2 single producer).

#include <juce_audio_basics/juce_audio_basics.h>

#include <vector>

#include "control/ControlDispatcher.h"
#include "control/ControlInbox.h"
#include "control/MidiMapper.h"
#include "control/OscSender.h"
#include "control/OscServer.h"

namespace spacer {

class ControlHub {
public:
    /// `cues` may be null. `project` is the live model (not copied).
    ControlHub(const Project& project, EngineProcessor& engine, CueEngine* cues);

    /// Applies project.osc and project.midi: (re)binds the OSC input port,
    /// (re)connects the output when enabled, loads the MIDI mappings. False if
    /// the input port could not be bound (the rest is still applied).
    bool applyProject();

    /// Any thread.
    void submitMidi(const juce::MidiMessage& message) { midiInbox_.push(message); }

    /// Message thread, ~30 Hz.
    void pump();
    /// Message thread: engine feedback for the OSC output.
    void onFeedback(const Feedback& feedback);

    OscServer& osc() noexcept { return server_; }
    OscSender& oscOut() noexcept { return sender_; }
    MidiMapper& midi() noexcept { return mapper_; }
    ControlDispatcher& dispatcher() noexcept { return dispatcher_; }

    /// Deliver time for tests: pump with an explicit clock.
    void pump(double nowEpochSeconds, double monotonicSeconds);

private:
    const Project& project_;
    OscServer server_;
    OscSender sender_;
    MidiMapper mapper_;
    ControlDispatcher dispatcher_;
    ControlInbox<juce::MidiMessage> midiInbox_;
    std::vector<juce::MidiMessage> midiScratch_;
    std::vector<ControlEvent> events_;
};

}  // namespace spacer
