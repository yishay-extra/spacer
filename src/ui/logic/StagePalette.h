#pragma once

// One colour per source id, spread by golden-ratio hue steps so neighbouring
// ids contrast. Plain C++, unit-tested on Linux.

#include <cstdint>

namespace spacer {

struct StageRgb {
    std::uint8_t r{0};
    std::uint8_t g{0};
    std::uint8_t b{0};
    bool operator==(const StageRgb&) const = default;
};

/// Hue in [0, 1): the fractional part of sourceId × 0.61803399 (golden-ratio steps).
float sourceHue(int sourceId);
/// HSV(sourceHue(id), 0.65, 1.0) as 8-bit RGB.
StageRgb sourceColour(int sourceId);

}  // namespace spacer
