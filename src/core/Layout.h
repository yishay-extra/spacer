#pragma once

#include <string>
#include <vector>

#include "core/Position.h"

namespace spacer {

inline constexpr int kMaxSpeakers = 32;
inline constexpr int kMaxSources = 64;
inline constexpr int kVirtualDeviceChannels = 64;

struct Speaker {
    int id{0};                // stable within a layout, >= 1
    std::string name;         // e.g. "L", "Rs", "Dome 7"
    Position position;        // physical position relative to listener
    float trimDb{0.0f};       // user trim, applied after compensation
    float userDelayMs{0.0f};  // user delay, added to compensation delay
    bool polarityInverted{false};
    int outputChannel{-1};  // 0-based device channel; -1 = unassigned
    bool lfe{false};        // LFE/subwoofer: never panned (SPEC §7)

    bool operator==(const Speaker&) const = default;
};

struct Layout {
    std::string name{"Untitled"};
    std::vector<Speaker> speakers;  // size in [2, kMaxSpeakers]

    bool operator==(const Layout&) const = default;

    const Speaker* findSpeaker(int id) const {
        for (const auto& s : speakers)
            if (s.id == id) return &s;
        return nullptr;
    }
};

/// Per-speaker values derived from geometry (see SPEC §7).
struct SpeakerCompensation {
    int speakerId{0};
    float delayMs{0.0f};  // so all wavefronts arrive at the origin together
    float gainDb{0.0f};   // 1/r equalisation relative to the farthest speaker
};

/// Compute compensation for every speaker in the layout. Implemented in
/// Layout.cpp (WP1). speedOfSound in m/s.
std::vector<SpeakerCompensation> computeCompensation(const Layout& layout, float speedOfSound = 343.0f);

}  // namespace spacer
