#pragma once

// Per-speaker processing state (SPEC §4, §5.3).

#include <vector>

#include "core/PluginSlot.h"

namespace spacer {

/// 3-band EQ: low shelf, mid peak, high shelf.
struct Eq3 {
    float lowFreq{100.0f}, lowGainDb{0.0f};
    float midFreq{1000.0f}, midGainDb{0.0f}, midQ{0.7f};
    float highFreq{8000.0f}, highGainDb{0.0f};
    bool operator==(const Eq3&) const = default;
};

struct SpeakerChainSpec {
    int speakerId{0};
    float trimDb{0.0f};
    float userDelayMs{0.0f};
    bool polarityInverted{false};
    bool mute{false};
    Eq3 eq;
    std::vector<PluginSlot> inserts;  // <= 4
    bool operator==(const SpeakerChainSpec&) const = default;
};

struct MasterSpec {
    float gainDb{0.0f};
    bool operator==(const MasterSpec&) const = default;
};

}  // namespace spacer
