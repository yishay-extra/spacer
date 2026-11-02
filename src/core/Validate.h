#pragma once

// Semantic validation of the model (SPEC §1, §7, §8, §10, §11). Parsing only
// checks structure; this checks that the values make sense together.

#include <string>
#include <vector>

#include "core/Project.h"

namespace spacer {

struct Issue {
    enum class Severity { Error, Warning };
    Severity severity{Severity::Error};
    std::string path;  // e.g. "layout.speakers[2].outputChannel"
    std::string message;
};

using Issues = std::vector<Issue>;

/// SPEC §7: 2..32 speakers, unique ids (>= 1), unique output channels (or -1),
/// distance > 0, finite values. Warns on coincident speaker directions.
Issues validate(const Layout& layout);

/// Source limits, motion/modulator/insert sanity. `layout` is used to resolve
/// speaker references (Direct targets, step targets); `pathPrefix` prefixes
/// issue paths.
Issues validate(const SourceSpec& source, const Layout& layout, const std::string& pathPrefix = "");

/// Whole project: layout, sources (<= 64, unique ids), scenes, speaker chains,
/// cue lists (scene references resolve), settings ranges.
Issues validate(const Project& project);

inline bool hasErrors(const Issues& issues) {
    for (const auto& i : issues)
        if (i.severity == Issue::Severity::Error) return true;
    return false;
}

}  // namespace spacer
