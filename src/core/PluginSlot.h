#pragma once

// Insert slot state (SPEC §13). Opaque to core.

#include <map>
#include <string>

namespace spacer {

struct PluginSlot {
    std::string pluginId;
    std::string stateBase64;
    std::map<int, float> params;  // parameter index -> normalised value
    bool operator==(const PluginSlot&) const = default;
};

}  // namespace spacer
