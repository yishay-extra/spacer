#pragma once

// Spacer coordinate system (see docs/SPEC-v1.md §2). Fixed; do not change.
//
//   Listener / layout origin at (0,0,0).
//   Cartesian: x = right, y = front, z = up, in metres.
//   Spherical: azimuth in degrees, 0 = front, positive = clockwise seen from
//              above (so +90 = right), range (-180, 180];
//              elevation in degrees, 0 = horizontal, +90 = straight up;
//              distance in metres, >= 0.

#include <algorithm>
#include <cmath>
#include <numbers>

namespace spacer {

inline constexpr float kPi = std::numbers::pi_v<float>;
inline constexpr float degToRad(float deg) { return deg * (kPi / 180.0f); }
inline constexpr float radToDeg(float rad) { return rad * (180.0f / kPi); }

struct Cartesian {
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};

    constexpr Cartesian operator+(const Cartesian& o) const { return {x + o.x, y + o.y, z + o.z}; }
    constexpr Cartesian operator-(const Cartesian& o) const { return {x - o.x, y - o.y, z - o.z}; }
    constexpr Cartesian operator*(float s) const { return {x * s, y * s, z * s}; }
    bool operator==(const Cartesian&) const = default;
    float length() const { return std::sqrt(x * x + y * y + z * z); }
    float dot(const Cartesian& o) const { return x * o.x + y * o.y + z * o.z; }
    constexpr Cartesian cross(const Cartesian& o) const {
        return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x};
    }
    Cartesian normalised() const {
        const float len = length();
        return len > 0.0f ? (*this) * (1.0f / len) : Cartesian{0.0f, 1.0f, 0.0f};
    }
};

struct Position {
    float azimuth{0.0f};    // degrees, (-180, 180]
    float elevation{0.0f};  // degrees, [-90, 90]
    float distance{1.0f};   // metres, >= 0

    bool operator==(const Position&) const = default;

    /// Wrap any angle in degrees into (-180, 180].
    static float wrapAzimuth(float deg) {
        deg = std::fmod(deg + 180.0f, 360.0f);
        if (deg < 0.0f) deg += 360.0f;
        deg -= 180.0f;
        return deg <= -180.0f ? 180.0f : deg;  // fmod result is >= -180 here
    }

    /// Clamp fields into their valid ranges.
    Position normalised() const {
        return {wrapAzimuth(azimuth), std::clamp(elevation, -90.0f, 90.0f), std::max(distance, 0.0f)};
    }

    Cartesian toCartesian() const {
        const float az = degToRad(azimuth);
        const float el = degToRad(elevation);
        const float horiz = distance * std::cos(el);
        return {horiz * std::sin(az), horiz * std::cos(az), distance * std::sin(el)};
    }

    static Position fromCartesian(const Cartesian& c) {
        const float d = c.length();
        if (d <= 0.0f) return {0.0f, 0.0f, 0.0f};
        const float az = radToDeg(std::atan2(c.x, c.y));
        const float el = radToDeg(std::asin(std::clamp(c.z / d, -1.0f, 1.0f)));
        return {wrapAzimuth(az), el, d};
    }

    /// Unit direction vector (ignores distance).
    Cartesian direction() const { return Position{azimuth, elevation, 1.0f}.toCartesian(); }
};

/// Linear interpolation in Cartesian space (avoids azimuth wrap artefacts).
/// t in [0,1].
inline Position lerp(const Position& a, const Position& b, float t) {
    const Cartesian ca = a.toCartesian();
    const Cartesian cb = b.toCartesian();
    return Position::fromCartesian(ca + (cb - ca) * t);
}

/// Angular distance between two directions, in degrees, [0, 180].
/// Uses atan2(|a×b|, a·b), which stays accurate near 0° and 180° where acos
/// of a dot product does not.
inline float angleBetween(const Position& a, const Position& b) {
    const Cartesian da = a.direction();
    const Cartesian db = b.direction();
    return radToDeg(std::atan2(da.cross(db).length(), da.dot(db)));
}

}  // namespace spacer
