#include "engine/PluginState.h"

namespace spacer::pluginstate {

std::string encode(const juce::MemoryBlock& data) {
    if (data.isEmpty()) return {};
    return juce::Base64::toBase64(data.getData(), data.getSize()).toStdString();
}

std::optional<juce::MemoryBlock> decode(const std::string& base64) {
    juce::MemoryBlock block;
    if (base64.empty()) return block;
    juce::MemoryOutputStream out(block, false);
    if (!juce::Base64::convertFromBase64(out, juce::String(base64))) return std::nullopt;
    out.flush();
    return block;
}

std::string capture(juce::AudioProcessor& processor) {
    juce::MemoryBlock state;
    processor.getStateInformation(state);
    return encode(state);
}

int applyParams(juce::AudioProcessor& processor, const std::map<int, float>& params) {
    const auto& list = processor.getParameters();
    int applied = 0;
    for (const auto& [index, value] : params) {
        if (index < 0 || index >= list.size()) continue;
        list.getUnchecked(index)->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, value));
        ++applied;
    }
    return applied;
}

bool restore(juce::AudioProcessor& processor, const PluginSlot& slot, std::string* error) {
    bool ok = true;
    if (!slot.stateBase64.empty()) {
        if (const auto state = decode(slot.stateBase64)) {
            if (state->getSize() > 0)
                processor.setStateInformation(state->getData(), static_cast<int>(state->getSize()));
        } else {
            ok = false;
            if (error) *error = "invalid stateBase64";
        }
    }
    applyParams(processor, slot.params);
    return ok;
}

std::map<int, float> readParams(const juce::AudioProcessor& processor, const std::map<int, float>& params) {
    const auto& list = processor.getParameters();
    std::map<int, float> out;
    for (const auto& [index, value] : params) {
        juce::ignoreUnused(value);
        if (index >= 0 && index < list.size()) out[index] = list.getUnchecked(index)->getValue();
    }
    return out;
}

}  // namespace spacer::pluginstate
