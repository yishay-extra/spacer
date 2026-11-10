#pragma once

// The fixed dot table every source cloud is drawn from, and the dot-count
// rules (loudness and frame budget). Plain C++, unit-tested on Linux.

#include <span>

namespace spacer {

struct CloudOffset {
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};
    float phase{0.0f};  // radians, [0, 2π)
};
inline constexpr int kCloudMaxDots = 96;
inline constexpr int kCloudMinBudgetDots = 24;
inline constexpr int kCloudFrameBudget = 6000;
inline constexpr int kGlowFrameBudget = 3000;
inline constexpr int kGlowMinDots = 3;

/// The one fixed table of kCloudMaxDots unit-variance offsets; identical on every platform.
std::span<const CloudOffset> cloudOffsets();
/// Most dots one cloud may use when `loudPoints` clouds are drawn:
/// clamp(kCloudFrameBudget / loudPoints, kCloudMinBudgetDots, kCloudMaxDots); kCloudMaxDots when loudPoints
/// <= 0.
int cloudDotLimit(int loudPoints);
/// Most dots one speaker glow may use: min(cloudLimit / 4, max(kGlowMinDots, kGlowFrameBudget / sharePairs));
/// cloudLimit / 4 when sharePairs <= 0; never below 0.
int glowDotLimit(int cloudLimit, int sharePairs);
/// round(fraction × limit) with fraction clamped to [0, 1]; at least 1 when
/// fraction > 0 and limit > 0; 0 when fraction is not finite or limit <= 0.
int cloudDots(float fraction, int limit);

}  // namespace spacer
