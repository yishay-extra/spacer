#include "ui/logic/CueListView.h"

#include "ui/logic/SceneEditing.h"
#include "ui/logic/TriggerText.h"

namespace spacer::cueview {

std::vector<Row> rows(const Project& project, const CueList& list, int current, int next) {
    std::vector<Row> out;
    out.reserve(list.cues.size());
    for (std::size_t i = 0; i < list.cues.size(); ++i) {
        Row r;
        r.index = static_cast<int>(i);
        r.current = r.index == current;
        r.next = r.index == next;
        r.sceneMissing = sceneedit::indexOfScene(project, list.cues[i].sceneName) < 0;
        out.push_back(r);
    }
    return out;
}

std::string currentText(const CueList& list, int current) {
    if (list.cues.empty()) return "Empty list";
    if (current < 0 || current >= static_cast<int>(list.cues.size())) return "Not started";
    return "Cue " + std::to_string(current + 1) + " of " + std::to_string(list.cues.size()) + ": " +
           list.cues[static_cast<std::size_t>(current)].name;
}

std::string nextText(const CueList& list, int next) {
    if (next < 0 || next >= static_cast<int>(list.cues.size())) return "End of list";
    const Cue& c = list.cues[static_cast<std::size_t>(next)];
    return "Next: " + c.name + " (" + triggertext::format(c.trigger) + ")";
}

Highlight highlight(const Row& row) {
    if (row.current && row.next) return Highlight::CurrentAndNext;
    if (row.current) return Highlight::Current;
    if (row.next) return Highlight::Next;
    return Highlight::None;
}

}  // namespace spacer::cueview
