#include "ui/logic/StageCloud.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace spacer {

namespace {

std::array<CloudOffset, kCloudMaxDots> buildTable() {
    std::uint32_t s = 0x53504143u;
    const auto next = [&s]() {
        s = s * 1664525u + 1013904223u;
        return static_cast<float>(s >> 8) / 16777216.0f;
    };
    // Four separate draws per coordinate: operand evaluation order is unspecified in C++.
    const auto coordinate = [&next]() {
        const float a = next();
        const float b = next();
        const float c = next();
        const float d = next();
        return (a + b + c + d - 2.0f) * std::sqrt(3.0f);
    };
    std::array<CloudOffset, kCloudMaxDots> table{};
    for (auto& entry : table) {
        entry.x = coordinate();
        entry.y = coordinate();
        entry.z = coordinate();
        entry.phase = next() * (2.0f * std::numbers::pi_v<float>);
    }
    return table;
}

}  // namespace

std::span<const CloudOffset> cloudOffsets() {
    static const std::array<CloudOffset, kCloudMaxDots> table = buildTable();
    return table;
}

int cloudDotLimit(int loudPoints) {
    if (loudPoints <= 0) return kCloudMaxDots;
    return std::clamp(kCloudFrameBudget / loudPoints, kCloudMinBudgetDots, kCloudMaxDots);
}

int glowDotLimit(int cloudLimit, int sharePairs) {
    const int base = std::max(cloudLimit, 0) / 4;
    if (sharePairs <= 0) return base;
    return std::min(base, std::max(kGlowMinDots, kGlowFrameBudget / sharePairs));
}

int cloudDots(float fraction, int limit) {
    if (!std::isfinite(fraction) || limit <= 0) return 0;
    const float f = std::clamp(fraction, 0.0f, 1.0f);
    const int n = static_cast<int>(std::lround(f * static_cast<float>(limit)));
    return (f > 0.0f) ? std::max(n, 1) : 0;
}

}  // namespace spacer
