#pragma once

// Hit-testing for the stage view: which source point or speaker is under the
// mouse. Plain C++, unit-tested on Linux.

#include <optional>
#include <vector>

#include "core/Position.h"
#include "ui/logic/StageTransform.h"

namespace spacer {

struct StageItem {
    enum class Kind { Source, Speaker };
    Kind kind{Kind::Source};
    int id{0};
    int channel{0};  // source point (0 = left / mono, 1 = right); 0 for speakers
    Cartesian position;
};

struct StageHitOptions {
    float sourceRadiusPx{12.0f};
    float speakerRadiusPx{14.0f};
    /// Only hit items of this kind (nullopt = any; sources win over speakers).
    std::optional<StageItem::Kind> only;
};

/// The item under `screen`. Sources are on top of speakers, so a source in
/// range wins over any speaker. Among items of one kind the one closest on
/// screen wins; at equal screen distance, the one nearer the viewer (larger
/// `StageTransform::depthOf`) wins, then the one listed first.
std::optional<StageItem> hitTest(const std::vector<StageItem>& items, const StageTransform& transform,
                                 const StagePoint& screen, const StageHitOptions& options = {});

}  // namespace spacer
