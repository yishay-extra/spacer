#include "ui/logic/InsertEditing.h"

namespace spacer::insertedit {

std::string fallbackName(const std::string& pluginId) {
    if (pluginId.empty()) return "(empty)";
    const auto cut = pluginId.find_last_of("/:");
    if (cut != std::string::npos && cut + 1 < pluginId.size()) return pluginId.substr(cut + 1);
    return pluginId;
}

std::vector<InsertRow> rows(const std::vector<PluginSlot>& slots) {
    std::vector<InsertRow> out;
    out.reserve(slots.size());
    for (const auto& slot : slots) out.push_back({fallbackName(slot.pluginId), slot.bypassed});
    return out;
}

const std::vector<PluginSlot>* slotsOf(const Project& project, InsertOwner owner, int ownerId) {
    if (owner == InsertOwner::Source) {
        for (const auto& s : project.sources)
            if (s.id == ownerId) return &s.inserts;
        return nullptr;
    }
    for (const auto& c : project.speakerChains)
        if (c.speakerId == ownerId) return &c.inserts;
    return nullptr;
}

}  // namespace spacer::insertedit
