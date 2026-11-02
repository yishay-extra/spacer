#pragma once

#include <cstddef>
#include <cstdint>

namespace spacer {

/// Numeric source-level parameters a modulator can target (SPEC §10). Units
/// are the model's: dB for gain, degrees for width, metres for rMin.
enum class SourceParam : std::uint8_t { GainDb, Spread, Width, DistanceK, DistanceRMin, DistanceQ, Count };

/// Numeric motion parameters a modulator can target. Which ones a motion
/// honours depends on its kind (`Trajectory::supports`). Periods, frequencies
/// and direction are deliberately not modulatable: phase is derived from the
/// clock, so modulating them would make the phase jump.
enum class MotionParam : std::uint8_t {
    PositionAz,  // Manual
    PositionEl,
    PositionDist,
    CentreAz,  // Orbit, Lissajous
    CentreEl,
    CentreDist,
    Radius,  // Orbit
    PhaseOffset,
    ElevationWobble,
    FromAz,  // Line
    FromEl,
    FromDist,
    ToAz,
    ToEl,
    ToDist,
    Ax,  // Lissajous amplitudes and phases
    Ay,
    Az,
    PhaseX,
    PhaseY,
    PhaseZ,
    StepMetres,  // RandomWalk
    Count
};

inline constexpr std::size_t kNumSourceParams = static_cast<std::size_t>(SourceParam::Count);
inline constexpr std::size_t kNumMotionParams = static_cast<std::size_t>(MotionParam::Count);

}  // namespace spacer
