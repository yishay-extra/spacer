#pragma once

// Text <-> value for the number boxes of the UI (WP11a's layout table has its
// own per-field parser; this is the shared one for the Sources panel).
// Locale independent ('.' decimal point); parsers accept an optional unit
// suffix and reject anything that is not a finite number. Plain C++,
// unit-tested on Linux.

#include <optional>
#include <string>

#include "core/MotionSpec.h"

namespace spacer::fmt {

enum class Unit {
    None,
    Db,       // "-6.0 dB"; "-inf" / "off" parse as kGainFloorDb
    Degrees,  // "30 deg", "30°"
    Metres,   // "1.5 m"
    Percent,  // stored as a fraction 0..1, shown "25 %"
    Seconds,  // "2.5 s"
    Millis,   // "20 ms"
    Hertz,    // "440 Hz"
    Count,    // integer-like, no suffix
};

/// Lowest gain a text box accepts ("-inf" maps here); finite so the model validates.
inline constexpr float kGainFloorDb = -100.0f;

/// `v` rounded to `decimals`, trailing zeros removed ("2", "2.5", "-0.25").
std::string number(double v, int decimals = 2);

/// Number plus unit suffix ("-6 dB", "30°", "1.5 m", "25 %", "20 ms", "440 Hz").
std::string format(float v, Unit unit);
/// The number alone, as shown in an edit field while typing.
std::string formatBare(float v, Unit unit);

/// Parses "12", "12.5", "-3 dB", "25%", ... for `unit`. nullopt if `text` is
/// not a finite number (optionally followed by that unit's suffix). Percent
/// returns a fraction (25 -> 0.25). The result is not clamped.
std::optional<float> parse(const std::string& text, Unit unit);
/// parse() then clamp to [min, max].
std::optional<float> parseClamped(const std::string& text, Unit unit, float min, float max);

/// "8 beats" / "2.5 s".
std::string formatPeriod(const Period& period);
/// "8", "8b", "8 beats", "2.5s", "2.5 sec": a bare number keeps the unit of
/// `current`. nullopt for non-positive / non-finite values.
std::optional<Period> parsePeriod(const std::string& text, const Period& current);

/// Trims ASCII whitespace.
std::string trim(const std::string& text);

}  // namespace spacer::fmt
