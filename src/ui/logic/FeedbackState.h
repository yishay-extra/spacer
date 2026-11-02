#pragma once

// What the UI knows about the running engine: the latest audio → control
// feedback (SPEC §5.2), polled at 30 Hz on the message thread, with meter
// ballistics for display. Plain C++ (engine/Feedback.h is JUCE-free),
// unit-tested on Linux.

#include <cstdint>
#include <map>
#include <optional>
#include <utility>

#include "core/Clock.h"
#include "core/Position.h"
#include "engine/Feedback.h"

namespace spacer {

class FeedbackState {
public:
    /// Meter fall-back rate in dB per second.
    static constexpr float kMeterFallDbPerSecond = 24.0f;
    /// Bottom of the meter scale.
    static constexpr float kMeterFloorDb = -60.0f;

    /// Takes one feedback item. CueFired is not stored (the app forwards it).
    void apply(const Feedback& feedback);
    /// Lets meters fall for `seconds` of display time.
    void decay(double seconds);

    /// Display level (peak with fall-back), dBFS, >= kMeterFloorDb.
    float speakerLevelDb(int speakerId) const;
    float speakerGainReductionDb(int speakerId) const;
    float sourceLevelDb(int sourceId) const;
    /// Rendered position of a source point, if the engine has reported one.
    std::optional<Position> sourcePosition(int sourceId, int channel) const;
    /// Forget rendered positions (the graph was rebuilt; old ones are stale).
    void clearPositions() { positions_.clear(); }

    const ClockState& clock() const { return clock_; }
    bool hasClock() const { return hasClock_; }
    int xruns() const { return xruns_; }
    std::uint64_t droppedCommands() const { return droppedCommands_; }

    /// 0 at kMeterFloorDb (and below), 1 at 0 dBFS (and above), linear in dB.
    static float meterFraction(float levelDb);
    static float toDb(float linear);

private:
    struct Meter {
        float levelDb{kMeterFloorDb};
        float gainReductionDb{0.0f};
    };
    static float level(const std::map<int, Meter>& meters, int id);

    std::map<int, Meter> speakers_;
    std::map<int, Meter> sources_;
    std::map<std::pair<int, int>, Position> positions_;
    ClockState clock_;
    bool hasClock_{false};
    int xruns_{0};
    std::uint64_t droppedCommands_{0};
};

}  // namespace spacer
