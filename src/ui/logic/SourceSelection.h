#pragma once

// The selected source (SPEC §16 item 2: the Sources panel shows the strip of
// the selected source). One model shared by the Sources panel, the stage view
// and — later — OSC / MIDI (`ControlDispatcher::Config::selectedSource`, which
// wants 0 for "none"). Plain C++, unit-tested on Linux. Message thread only.

#include <cstdint>
#include <optional>

#include "core/Project.h"

namespace spacer {

class SourceSelection {
public:
    /// Selects `sourceId`. Returns true if the selection changed. The id is
    /// not checked against a project; call reconcile() after model changes.
    bool select(int sourceId);
    bool clear();

    std::optional<int> selected() const noexcept { return id_ > 0 ? std::optional<int>(id_) : std::nullopt; }
    /// 0 when nothing is selected: the form `ControlDispatcher::Config::selectedSource` returns.
    int selectedId() const noexcept { return id_; }
    bool isSelected(int sourceId) const noexcept { return id_ > 0 && id_ == sourceId; }

    /// Makes the selection valid for `project`: a selected source that no
    /// longer exists is replaced by `fallbackId` if that exists (see
    /// neighbourAfterRemoval), else by the first source, else cleared. With
    /// nothing selected and sources present, selects the first. True if changed.
    bool reconcile(const Project& project, int fallbackId = 0);

    /// Incremented on every change.
    std::uint64_t revision() const noexcept { return revision_; }

private:
    int id_{0};
    std::uint64_t revision_{0};
};

/// The source to select after `removedId` was removed from `before`: the one
/// that followed it in list order, else the one before it, else 0.
int neighbourAfterRemoval(const Project& before, int removedId);

/// Selection moved by `delta` rows in list order from `current` (clamped, no
/// wrap). With no / unknown current selection: the first source (delta >= 0)
/// or the last (delta < 0). 0 if there are no sources.
int stepSelection(const Project& project, int current, int delta);

}  // namespace spacer
