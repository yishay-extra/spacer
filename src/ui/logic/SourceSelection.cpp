#include "ui/logic/SourceSelection.h"

#include <algorithm>

namespace spacer {

namespace {

bool hasSource(const Project& project, int id) {
    return std::any_of(project.sources.begin(), project.sources.end(),
                       [id](const SourceSpec& s) { return s.id == id; });
}

int indexOf(const Project& project, int id) {
    for (std::size_t i = 0; i < project.sources.size(); ++i)
        if (project.sources[i].id == id) return static_cast<int>(i);
    return -1;
}

}  // namespace

bool SourceSelection::select(int sourceId) {
    const int next = sourceId > 0 ? sourceId : 0;
    if (next == id_) return false;
    id_ = next;
    ++revision_;
    return true;
}

bool SourceSelection::clear() {
    return select(0);
}

bool SourceSelection::reconcile(const Project& project, int fallbackId) {
    if (id_ > 0 && hasSource(project, id_)) return false;
    int next = 0;
    if (fallbackId > 0 && hasSource(project, fallbackId))
        next = fallbackId;
    else if (!project.sources.empty())
        next = project.sources.front().id;
    return select(next);
}

int neighbourAfterRemoval(const Project& before, int removedId) {
    const int i = indexOf(before, removedId);
    if (i < 0) return 0;
    const auto n = static_cast<int>(before.sources.size());
    if (i + 1 < n) return before.sources[static_cast<std::size_t>(i + 1)].id;
    if (i > 0) return before.sources[static_cast<std::size_t>(i - 1)].id;
    return 0;
}

int stepSelection(const Project& project, int current, int delta) {
    if (project.sources.empty()) return 0;
    const int i = indexOf(project, current);
    const auto last = static_cast<int>(project.sources.size()) - 1;
    const int target = i < 0 ? (delta >= 0 ? 0 : last) : std::clamp(i + delta, 0, last);
    return project.sources[static_cast<std::size_t>(target)].id;
}

}  // namespace spacer
