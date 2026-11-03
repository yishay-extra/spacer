#pragma once

namespace spacer {

/// SMPTE frame rates as coded in MTC (2-bit rate field).
enum class TimecodeRate { Fps24 = 0, Fps25 = 1, Fps2997Drop = 2, Fps30 = 3 };

/// An SMPTE time as carried by MIDI Time Code.
struct Timecode {
    int hours{0}, minutes{0}, seconds{0}, frames{0};
    TimecodeRate rate{TimecodeRate::Fps25};

    /// Nominal frames per second label (24, 25, 30, 30) used for counting.
    static int frameCount(TimecodeRate r);
    /// Real frames per second (29.97 for drop frame).
    static double framesPerSecond(TimecodeRate r);
    /// Real seconds since 00:00:00:00 (drop-frame aware).
    double toSeconds() const;

    bool operator==(const Timecode&) const = default;
};

}  // namespace spacer
