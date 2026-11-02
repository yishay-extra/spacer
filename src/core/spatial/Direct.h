#pragma once

#include <array>
#include <span>
#include <string_view>
#include <vector>

#include "core/Layout.h"
#include "core/Spatializer.h"

namespace spacer {

/// Direct assignment (SPEC §6.4): the source goes to the speakers named by
/// id, with unit gain each. Position and spread are ignored for rendering.
///
/// - Speaker ids that are not in the layout are ignored.
/// - Unlike VBAP/DBAP this does route to `lfe` speakers (SPEC §7: they receive
///   signal only via Direct).
/// - No normalisation: N targets each get gain 1.
class Direct final : public Spatializer {
public:
    explicit Direct(std::vector<int> speakerIds = {});

    std::string_view name() const override { return "direct"; }

    void prepare(const Layout& layout, double sampleRate, int maxBlockSize) override;

    void render(const float* mono, int numSamples, const Position& start, const Position& end, float spread,
                SpeakerBus& bus) override;

    void gainsFor(const Position& pos, float spread, std::span<float> outGains) const override;

    /// Layout indices this instance writes to (after prepare()).
    std::span<const int> targets() const { return {targets_.data(), static_cast<std::size_t>(numTargets_)}; }

private:
    std::vector<int> speakerIds_;
    std::array<int, kMaxSpeakers> targets_{};
    int numTargets_{0};
};

}  // namespace spacer
