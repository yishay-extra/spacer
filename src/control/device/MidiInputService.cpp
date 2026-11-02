#include "control/device/MidiInputService.h"

namespace spacer {

int MidiInputService::openAll() {
    close();
    for (const auto& info : juce::MidiInput::getAvailableDevices())
        if (auto input = juce::MidiInput::openDevice(info.identifier, this)) {
            input->start();
            inputs_.push_back(std::move(input));
        }
    return static_cast<int>(inputs_.size());
}

void MidiInputService::close() {
    for (auto& i : inputs_) i->stop();
    inputs_.clear();
}

}  // namespace spacer
