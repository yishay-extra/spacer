#pragma once

// MIDI device input (app only: needs juce_audio_devices). Opens every
// available input and forwards messages to `sink`, which is called on the
// MIDI thread: it must only hand the message over (ControlHub::submitMidi).

#include <juce_audio_devices/juce_audio_devices.h>

#include <functional>
#include <memory>
#include <vector>

namespace spacer {

class MidiInputService : private juce::MidiInputCallback {
public:
    using Sink = std::function<void(const juce::MidiMessage&)>;
    explicit MidiInputService(Sink sink) : sink_(std::move(sink)) {}
    ~MidiInputService() override { close(); }

    /// Opens all currently available inputs. Returns how many were opened.
    int openAll();
    void close();

private:
    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& m) override { sink_(m); }

    Sink sink_;
    std::vector<std::unique_ptr<juce::MidiInput>> inputs_;
};

}  // namespace spacer
