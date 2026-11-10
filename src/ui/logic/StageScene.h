#pragma once

// Everything the stage view draws, gathered from the model and the engine
// feedback: speakers, source points (rendered positions when the engine has
// reported them, else the model's phase-0 positions) and trajectory previews.
// Plain C++, unit-tested on Linux.

#include <array>
#include <span>
#include <string>
#include <vector>

#include "core/Project.h"
#include "ui/logic/FeedbackState.h"
#include "ui/logic/StageHitTest.h"
#include "ui/logic/StagePalette.h"
#include "ui/logic/StageShares.h"

namespace spacer {

inline constexpr float kMinDrawnShare = 0.03f;
inline constexpr int kMaxShareLines = 8;

/// Layout speaker indices of the strongest non-zero shares, strongest first (ties: lower index first),
/// at most out.size() of them. Returns how many were written.
int strongestShares(std::span<const float> shares, std::span<int> out);

struct StageSpeaker {
    int id{0};
    std::string name;
    Cartesian position;
    bool lfe{false};
    float levelDb{FeedbackState::kMeterFloorDb};
    float gainReductionDb{0.0f};
};

struct StageSourcePoint {
    int id{0};
    int channel{0};
    std::string name;
    Cartesian position;
    bool mute{false};
    StageRgb colour;
    float level{0.0f};
    bool hasShares{false};
    std::array<float, kMaxSpeakers> shares{};
};

struct StageTrajectory {
    int sourceId{0};
    int channel{0};
    std::vector<Cartesian> points;
};

class StageScene {
public:
    /// Rebuilds the trajectory previews; call when the model changed.
    void setProject(const Project& project);
    /// Refreshes positions and meters; call on every feedback poll.
    void update(const Project& project, const FeedbackState& feedback);

    const std::vector<StageSpeaker>& speakers() const { return speakers_; }
    const std::vector<StageSourcePoint>& sources() const { return sources_; }
    const std::vector<StageTrajectory>& trajectories() const { return trajectories_; }

    /// Source points with level > 0.
    int loudPoints() const { return loudPoints_; }
    /// (Source point, speaker) pairs with a share > 0.
    int sharePairs() const { return sharePairs_; }

    /// Hit-test items (sources and speakers) for StageHitTest.
    std::vector<StageItem> items() const;

private:
    std::vector<StageSpeaker> speakers_;
    std::vector<StageSourcePoint> sources_;
    std::vector<StageTrajectory> trajectories_;
    StageShares shares_;
    Layout sharesLayout_;
    bool sharesPrepared_{false};
    int loudPoints_{0};
    int sharePairs_{0};
};

}  // namespace spacer
