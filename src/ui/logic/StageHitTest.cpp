#include "ui/logic/StageHitTest.h"

#include <cmath>
#include <limits>

namespace spacer {

std::optional<StageItem> hitTest(const std::vector<StageItem>& items, const StageTransform& transform,
                                 const StagePoint& screen, const StageHitOptions& options) {
    const StageItem* best[2] = {nullptr, nullptr};
    float bestDist[2] = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    float bestDepth[2] = {0.0f, 0.0f};

    for (const auto& item : items) {
        if (options.only && *options.only != item.kind) continue;
        const int k = item.kind == StageItem::Kind::Source ? 0 : 1;
        const float radius = k == 0 ? options.sourceRadiusPx : options.speakerRadiusPx;
        const StagePoint p = transform.toScreen(item.position);
        const float d = std::hypot(p.x - screen.x, p.y - screen.y);
        if (d > radius) continue;
        const float depth = transform.depthOf(item.position);
        if (best[k] == nullptr || d < bestDist[k] || (!(d > bestDist[k]) && depth > bestDepth[k])) {
            best[k] = &item;
            bestDist[k] = d;
            bestDepth[k] = depth;
        }
    }
    if (best[0] != nullptr) return *best[0];
    if (best[1] != nullptr) return *best[1];
    return std::nullopt;
}

}  // namespace spacer
