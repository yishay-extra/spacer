#include "core/motion/Lfo.h"

#include <cmath>

#include "core/Position.h"
#include "core/motion/MotionTime.h"
#include "core/motion/Rng.h"

namespace spacer {

float lfoValue(LfoShape shape, double cycles, std::uint32_t seed) {
    const auto p = static_cast<float>(fractional(cycles));
    switch (shape) {
        case LfoShape::Sine:
            return std::sin(2.0f * kPi * p);
        case LfoShape::Triangle:
            if (p < 0.25f) return 4.0f * p;
            if (p < 0.75f) return 2.0f - 4.0f * p;
            return 4.0f * p - 4.0f;
        case LfoShape::Saw:
            return 2.0f * p - 1.0f;
        case LfoShape::Square:
            return p < 0.5f ? 1.0f : -1.0f;
        case LfoShape::SampleAndHold: {
            const auto index = static_cast<std::int64_t>(std::floor(cycles));
            return static_cast<float>(2.0 * Rng::hashUniform(seed, index) - 1.0);
        }
    }
    return 0.0f;
}

}  // namespace spacer
