#pragma once

// Undo / redo of model edits (PLAN WP11 acceptance: "undo/redo for model
// edits"). Plain C++, unit-tested on Linux; the app owns one per project.

#include <cstddef>
#include <deque>
#include <optional>
#include <string>

#include "ui/logic/ProjectEdit.h"

namespace spacer {

class ProjectHistory {
public:
    explicit ProjectHistory(std::size_t limit = 200) : limit_(limit) {}

    /// Applies `edit` to `project` and records it (clearing the redo list).
    /// While a gesture is open, an edit with the same non-empty mergeKey as
    /// the last one replaces that step's "apply" and keeps its "revert", so
    /// one drag is one undo step. Returns the edit's engine effect, or
    /// nullopt if the edit was invalid (nothing happened).
    std::optional<EngineEffect> perform(Project& project, ProjectEdit edit);

    /// Start / end of a continuous gesture (mouse drag). Ending it stops
    /// further merging.
    void beginGesture() {
        gestureOpen_ = true;
        lastMergeable_ = false;
    }
    void endGesture() { gestureOpen_ = false; }
    bool gestureOpen() const { return gestureOpen_; }

    /// Undo / redo one step; nullopt if there is nothing to do. Ends an open gesture.
    std::optional<EngineEffect> undo(Project& project);
    std::optional<EngineEffect> redo(Project& project);

    bool canUndo() const { return !done_.empty(); }
    bool canRedo() const { return !undone_.empty(); }
    std::string undoName() const { return canUndo() ? done_.back().name : std::string{}; }
    std::string redoName() const { return canRedo() ? undone_.back().name : std::string{}; }
    std::size_t undoDepth() const { return done_.size(); }

    /// Forget everything (new / loaded project).
    void clear();

private:
    std::size_t limit_;
    std::deque<ProjectEdit> done_;
    std::deque<ProjectEdit> undone_;
    bool gestureOpen_{false};
    bool lastMergeable_{false};
};

}  // namespace spacer
