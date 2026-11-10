#include "ui/logic/StagePalette.h"

#include <cmath>

namespace spacer {

namespace {

constexpr double kGoldenStep = 0.61803399;
constexpr float kSaturation = 0.65f;

std::uint8_t toByte(float v) {
    return static_cast<std::uint8_t>(std::lround(v * 255.0f));
}

}  // namespace

float sourceHue(int sourceId) {
    const double scaled = static_cast<double>(sourceId) * kGoldenStep;
    const double h = scaled - std::floor(scaled);
    const float f = static_cast<float>(h);
    return f < 1.0f ? f : 0.0f;
}

StageRgb sourceColour(int sourceId) {
    const float h6 = sourceHue(sourceId) * 6.0f;
    const int sector = static_cast<int>(std::floor(h6));
    const float frac = h6 - static_cast<float>(sector);
    const float p = 1.0f - kSaturation;
    const float q = 1.0f - kSaturation * frac;
    const float t = 1.0f - kSaturation * (1.0f - frac);
    float r = 1.0f, g = t, b = p;
    switch (sector) {
        case 1:
            r = q;
            g = 1.0f;
            b = p;
            break;
        case 2:
            r = p;
            g = 1.0f;
            b = t;
            break;
        case 3:
            r = p;
            g = q;
            b = 1.0f;
            break;
        case 4:
            r = t;
            g = p;
            b = 1.0f;
            break;
        case 5:
            r = 1.0f;
            g = p;
            b = q;
            break;
        default:
            break;
    }
    return {toByte(r), toByte(g), toByte(b)};
}

}  // namespace spacer
