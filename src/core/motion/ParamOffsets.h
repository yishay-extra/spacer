#pragma once

#include <array>
#include <cstddef>

#include "core/motion/MotionParam.h"

namespace spacer {

/// Additive offsets per parameter, in the parameter's own units. Produced by a
/// ModulatorBank each block and added to the spec values (SPEC §10: modulators
/// are applied additively). Fixed size, so copying is real-time safe.
template <typename Param, std::size_t N>
struct ParamOffsets {
    std::array<float, N> values{};

    float operator[](Param p) const { return values[static_cast<std::size_t>(p)]; }
    float& operator[](Param p) { return values[static_cast<std::size_t>(p)]; }
    void clear() { values.fill(0.0f); }
    bool operator==(const ParamOffsets&) const = default;
};

using MotionModulation = ParamOffsets<MotionParam, kNumMotionParams>;
using SourceModulation = ParamOffsets<SourceParam, kNumSourceParams>;

}  // namespace spacer
