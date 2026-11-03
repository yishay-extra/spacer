#pragma once

// What the Cues panel shows for the armed list: one row state per cue
// (current / next highlighting, missing scene) and the header texts. Plain
// C++, unit-tested on Linux.

#include <string>
#include <vector>

#include "core/Project.h"

namespace spacer::cueview {

struct Row {
    int index{0};
    bool current{false};       // fired last
    bool next{false};          // Go fires this
    bool sceneMissing{false};  // the cue's scene does not exist in the project
};

/// `current` / `next` as reported by CueSequencer (-1 = none).
std::vector<Row> rows(const Project& project, const CueList& list, int current, int next);

/// "Cue 3 of 8: Verse" / "Not started" / "Empty list".
std::string currentText(const CueList& list, int current);
/// "Next: Chorus (after 4 s)" / "Next: Chorus (manual)" / "End of list".
std::string nextText(const CueList& list, int next);

/// Where the row's text colour / background should come from.
enum class Highlight { None, Current, Next, CurrentAndNext };
Highlight highlight(const Row& row);

}  // namespace spacer::cueview
