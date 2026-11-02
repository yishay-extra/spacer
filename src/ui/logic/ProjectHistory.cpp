#include "ui/logic/ProjectHistory.h"

#include <utility>

namespace spacer {

namespace {

EngineEffect combine(const EngineEffect& a, const EngineEffect& b) {
    if (a.kind == b.kind && a.id == b.id) return a;
    if (a.kind == EngineEffect::Kind::None) return b;
    if (b.kind == EngineEffect::Kind::None) return a;
    return EngineEffect::structural();
}

}  // namespace

std::optional<EngineEffect> ProjectHistory::perform(Project& project, ProjectEdit edit) {
    if (!edits::isValid(edit)) return std::nullopt;
    edit.apply(project);
    const EngineEffect effect = edit.effect;
    undone_.clear();

    const bool merge = gestureOpen_ && lastMergeable_ && !edit.mergeKey.empty() && !done_.empty() &&
                       done_.back().mergeKey == edit.mergeKey;
    if (merge) {
        ProjectEdit& top = done_.back();
        top.apply = std::move(edit.apply);
        top.effect = combine(top.effect, edit.effect);
    } else {
        done_.push_back(std::move(edit));
        while (done_.size() > limit_) done_.pop_front();
    }
    lastMergeable_ = gestureOpen_;
    return effect;
}

std::optional<EngineEffect> ProjectHistory::undo(Project& project) {
    endGesture();
    lastMergeable_ = false;
    if (done_.empty()) return std::nullopt;
    ProjectEdit e = std::move(done_.back());
    done_.pop_back();
    e.revert(project);
    const EngineEffect effect = e.effect;
    undone_.push_back(std::move(e));
    return effect;
}

std::optional<EngineEffect> ProjectHistory::redo(Project& project) {
    endGesture();
    lastMergeable_ = false;
    if (undone_.empty()) return std::nullopt;
    ProjectEdit e = std::move(undone_.back());
    undone_.pop_back();
    e.apply(project);
    const EngineEffect effect = e.effect;
    done_.push_back(std::move(e));
    return effect;
}

void ProjectHistory::clear() {
    done_.clear();
    undone_.clear();
    gestureOpen_ = false;
    lastMergeable_ = false;
}

}  // namespace spacer
