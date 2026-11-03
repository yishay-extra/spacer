#pragma once

// Bookkeeping for plug-in editor windows (SPEC §13, WP11e): which insert slots
// have an editor window open and which plug-in instance each one shows.
// Windows belong to a *slot*, not to an instance: a graph swap or a scene
// recall may replace the instance behind a slot (PluginHost keeps the newest
// one per slot), and the window then follows it. A window closes when its
// slot goes away or now holds a different plug-in. Plain C++ (instances are
// opaque tokens), unit-tested on Linux; PluginUi owns the actual windows.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "core/InsertParamPath.h"
#include "core/Project.h"

namespace spacer::pluginwindows {

struct SlotKey {
    InsertOwner owner{InsertOwner::Source};
    int ownerId{0};
    int slot{0};
    bool operator==(const SlotKey&) const = default;
};

/// Identity of the live instance behind a slot; 0 = none (missing plug-in,
/// failed to load, or the slot is empty).
using InstanceToken = std::uintptr_t;
using InstanceLookup = std::function<InstanceToken(const SlotKey&)>;

/// pluginId of the slot in the project; empty if there is no such slot or it is empty.
std::string pluginIdAt(const Project& project, const SlotKey& key);

struct Entry {
    SlotKey key;
    std::string pluginId;
    InstanceToken instance{0};
};

class WindowBook {
public:
    enum class OpenResult {
        Opened,       // a new window is needed
        AlreadyOpen,  // bring the existing one to the front
        NoSuchSlot,   // the slot does not exist or is empty
        NotLoaded,    // the plug-in is missing / failed to load: nothing to show
    };
    OpenResult open(const Project& project, const SlotKey& key, const InstanceLookup& lookup);
    /// The user closed the window of `key`.
    void closed(const SlotKey& key);

    struct Plan {
        std::vector<SlotKey> close;   // windows to close (already removed from the book)
        std::vector<Entry> retarget;  // windows to rebuild around the new instance (book updated)
        bool empty() const { return close.empty() && retarget.empty(); }
    };
    /// Compares the open windows with the project and the registry. Call after
    /// any model change, graph swap or recall, and on a timer.
    Plan reconcile(const Project& project, const InstanceLookup& lookup);

    bool isOpen(const SlotKey& key) const;
    const Entry* find(const SlotKey& key) const;
    const std::vector<Entry>& entries() const noexcept { return entries_; }

private:
    std::vector<Entry> entries_;
};

}  // namespace spacer::pluginwindows
