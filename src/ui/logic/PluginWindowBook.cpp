#include "ui/logic/PluginWindowBook.h"

#include <algorithm>

#include "ui/logic/InsertEditing.h"

namespace spacer::pluginwindows {

std::string pluginIdAt(const Project& project, const SlotKey& key) {
    const auto* slots = insertedit::slotsOf(project, key.owner, key.ownerId);
    if (slots == nullptr || key.slot < 0 || key.slot >= static_cast<int>(slots->size())) return {};
    return (*slots)[static_cast<std::size_t>(key.slot)].pluginId;
}

const Entry* WindowBook::find(const SlotKey& key) const {
    for (const auto& e : entries_)
        if (e.key == key) return &e;
    return nullptr;
}

bool WindowBook::isOpen(const SlotKey& key) const {
    return find(key) != nullptr;
}

WindowBook::OpenResult WindowBook::open(const Project& project, const SlotKey& key,
                                        const InstanceLookup& lookup) {
    if (isOpen(key)) return OpenResult::AlreadyOpen;
    const std::string pluginId = pluginIdAt(project, key);
    if (pluginId.empty()) return OpenResult::NoSuchSlot;
    const InstanceToken instance = lookup(key);
    if (instance == 0) return OpenResult::NotLoaded;
    entries_.push_back({key, pluginId, instance});
    return OpenResult::Opened;
}

void WindowBook::closed(const SlotKey& key) {
    entries_.erase(
        std::remove_if(entries_.begin(), entries_.end(), [&](const Entry& e) { return e.key == key; }),
        entries_.end());
}

WindowBook::Plan WindowBook::reconcile(const Project& project, const InstanceLookup& lookup) {
    Plan plan;
    for (auto it = entries_.begin(); it != entries_.end();) {
        const InstanceToken live =
            pluginIdAt(project, it->key) == it->pluginId && !it->pluginId.empty() ? lookup(it->key) : 0;
        if (live == 0) {
            plan.close.push_back(it->key);
            it = entries_.erase(it);
            continue;
        }
        if (live != it->instance) {
            it->instance = live;
            plan.retarget.push_back(*it);
        }
        ++it;
    }
    return plan;
}

}  // namespace spacer::pluginwindows
