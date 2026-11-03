#pragma once

// Insert slot state (SPEC §13). Opaque to core: the engine's PluginHost turns
// it into a plugin instance.

#include <map>
#include <string>

namespace spacer {

struct PluginSlot {
    /// The plugin's identifier as the host knows it (for AudioUnits JUCE's
    /// `PluginDescription::fileOrIdentifier`, e.g. "AudioUnit:Effects/aufx,dely,appl").
    std::string pluginId;
    /// The plugin's own state blob (getStateInformation), standard base64.
    /// Empty = the plugin's defaults.
    std::string stateBase64;
    std::map<int, float> params;  // parameter index -> normalised value, applied after the state
    /// User bypass: the slot passes its input through (latency-aligned).
    /// JSON key optional, default false.
    bool bypassed{false};
    bool operator==(const PluginSlot&) const = default;
};

}  // namespace spacer
