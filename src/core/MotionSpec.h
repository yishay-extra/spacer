#pragma once

// Motion parameter model (SPEC §10). Pure data; the evaluators live elsewhere
// (WP7). Periods are beats or seconds (SPEC §9).

#include <array>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include "core/ExactEqual.h"
#include "core/Position.h"

namespace spacer {

struct Period {
    enum class Unit { Beats, Seconds };
    Unit unit{Unit::Beats};
    double value{8.0};

    static Period beats(double v) { return {Unit::Beats, v}; }
    static Period seconds(double v) { return {Unit::Seconds, v}; }
    bool operator==(const Period& o) const { return unit == o.unit && exactlyEqual(value, o.value); }
};

enum class PlayMode { Once, Loop, PingPong };
enum class Easing { Linear, Smooth };
enum class PathInterpolation { Linear, CatmullRom };
enum class StepOrder { Forward, PingPong, Random };
enum class StepTransition { Jump, Glide };

struct ManualMotion {
    Position position{0.0f, 0.0f, 1.0f};
    float smoothMs{20.0f};
    bool operator==(const ManualMotion&) const = default;
};

struct OrbitMotion {
    Position centre{0.0f, 0.0f, 0.0f};
    float radius{1.0f};
    Period period{Period::beats(16)};
    int direction{1};             // +1 clockwise (seen from above), -1 counter-clockwise
    float phaseOffset{0.0f};      // degrees
    float elevationWobble{0.0f};  // degrees peak
    Period wobblePeriod{Period::beats(16)};
    bool operator==(const OrbitMotion&) const = default;
};

struct LineMotion {
    Position from{-90.0f, 0.0f, 2.0f};
    Position to{90.0f, 0.0f, 2.0f};
    Period period{Period::beats(8)};
    PlayMode mode{PlayMode::PingPong};
    Easing easing{Easing::Linear};
    bool operator==(const LineMotion&) const = default;
};

struct LissajousMotion {
    Position centre{0.0f, 0.0f, 0.0f};
    float ax{1.0f}, ay{1.0f}, az{0.0f};  // amplitudes, metres
    float fx{1.0f}, fy{2.0f}, fz{0.0f};  // cycles per period
    Period period{Period::beats(16)};
    std::array<float, 3> phases{0.0f, 0.0f, 0.0f};  // degrees (x, y, z)
    bool operator==(const LissajousMotion&) const = default;
};

struct PathMotion {
    std::vector<Cartesian> points;
    Period period{Period::beats(16)};
    PlayMode mode{PlayMode::Loop};
    PathInterpolation interpolation{PathInterpolation::Linear};
    bool constantSpeed{false};
    bool operator==(const PathMotion&) const = default;
};

struct BoxBounds {
    Cartesian min{-2.0f, -2.0f, 0.0f};
    Cartesian max{2.0f, 2.0f, 2.0f};
    bool operator==(const BoxBounds&) const = default;
};
struct SphereBounds {
    Cartesian centre{0.0f, 0.0f, 0.0f};
    float radius{2.0f};
    bool operator==(const SphereBounds&) const = default;
};

struct RandomWalkMotion {
    std::variant<BoxBounds, SphereBounds> bounds{SphereBounds{}};
    float stepMetres{0.5f};
    Period stepPeriod{Period::beats(1)};
    std::uint32_t seed{1};
    bool glide{true};
    bool operator==(const RandomWalkMotion&) const = default;
};

struct Step {
    /// Either a fixed position or a speaker id (move to that speaker).
    std::variant<Position, int> target{Position{}};
    Period duration{Period::beats(1)};
    StepTransition transition{StepTransition::Jump};
    Period glideTime{Period::beats(0.25)};
    float probability{1.0f};  // 0..1, chance this step is taken
    bool operator==(const Step&) const = default;
};

struct StepSequenceMotion {
    std::vector<Step> steps;
    StepOrder order{StepOrder::Forward};
    bool loop{true};
    std::uint32_t seed{1};  // for Random order / probability (determinism)
    bool operator==(const StepSequenceMotion&) const = default;
};

/// Tagged union of every motion kind. Default is Manual.
struct MotionSpec {
    using Variant = std::variant<ManualMotion, OrbitMotion, LineMotion, LissajousMotion, PathMotion,
                                 RandomWalkMotion, StepSequenceMotion>;
    Variant value{ManualMotion{}};
    bool operator==(const MotionSpec&) const = default;
};

// ---- Modulators (SPEC §10) -------------------------------------------------

enum class LfoShape { Sine, Triangle, Saw, Square, SampleAndHold };

struct LfoSource {
    LfoShape shape{LfoShape::Sine};
    Period period{Period::beats(4)};
    bool operator==(const LfoSource&) const = default;
};
struct OscSource {
    std::string address;
    bool operator==(const OscSource&) const = default;
};
struct MidiCcSource {
    int cc{0};
    bool operator==(const MidiCcSource&) const = default;
};

struct ModulatorSpec {
    std::variant<LfoSource, OscSource, MidiCcSource> source{LfoSource{}};
    std::string target;  // parameter path, e.g. "motion.radius", "gain"
    float depth{1.0f};
    float offset{0.0f};
    bool operator==(const ModulatorSpec&) const = default;
};

}  // namespace spacer
