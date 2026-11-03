#include "control/device/MidiInputService.h"

#include <algorithm>

namespace spacer {

std::vector<std::string> MidiInputService::availableNames() {
    std::vector<std::string> names;
    for (const auto& info : juce::MidiInput::getAvailableDevices()) names.push_back(info.name.toStdString());
    return names;
}

int MidiInputService::openAll(const std::vector<std::string>& disabledNames) {
    close();
    for (const auto& info : juce::MidiInput::getAvailableDevices()) {
        if (std::find(disabledNames.begin(), disabledNames.end(), info.name.toStdString()) !=
            disabledNames.end())
            continue;
        if (auto input = juce::MidiInput::openDevice(info.identifier, this)) {
            input->start();
            inputs_.push_back(std::move(input));
        }
    }
    return static_cast<int>(inputs_.size());
}

void MidiInputService::close() {
    for (auto& i : inputs_) i->stop();
    inputs_.clear();
}

}  // namespace spacer
