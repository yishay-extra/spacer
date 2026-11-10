#include "ui/logic/StageDepth.h"

#include <algorithm>
#include <cmath>

namespace spacer {

std::vector<std::size_t> farToNear(std::span<const Cartesian> positions, const StageTransform& transform) {
    std::vector<std::size_t> order;
    order.reserve(positions.size());
    for (std::size_t i = 0; i < positions.size(); ++i) {
        const Cartesian& c = positions[i];
        if (std::isfinite(c.x) && std::isfinite(c.y) && std::isfinite(c.z)) order.push_back(i);
    }
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        return transform.depthOf(positions[a]) < transform.depthOf(positions[b]);
    });
    return order;
}

float depthCue(const Cartesian& c, const StageTransform& transform) {
    const float extent = transform.extent();
    const float t = std::clamp((transform.depthOf(c) + extent) / (2.0f * extent), 0.0f, 1.0f);
    return kFarScale + (1.0f - kFarScale) * t;
}

}  // namespace spacer
