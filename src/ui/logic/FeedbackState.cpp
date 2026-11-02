#include "ui/logic/FeedbackState.h"

#include <algorithm>
#include <cmath>
#include <type_traits>

namespace spacer {

float FeedbackState::toDb(float linear) {
    if (!(linear > 0.0f) || !std::isfinite(linear)) return kMeterFloorDb;
    return std::max(kMeterFloorDb, 20.0f * std::log10(linear));
}

float FeedbackState::meterFraction(float levelDb) {
    if (!std::isfinite(levelDb)) return levelDb > 0.0f ? 1.0f : 0.0f;
    return std::clamp((levelDb - kMeterFloorDb) / -kMeterFloorDb, 0.0f, 1.0f);
}

void FeedbackState::apply(const Feedback& feedback) {
    std::visit(
        [this](const auto& f) {
            using T = std::decay_t<decltype(f)>;
            if constexpr (std::is_same_v<T, fb::SpeakerMeter>) {
                Meter& m = speakers_[f.speakerId];
                m.levelDb = std::max(m.levelDb, toDb(f.peak));
                m.gainReductionDb = f.gainReductionDb;
            } else if constexpr (std::is_same_v<T, fb::SourceMeter>) {
                Meter& m = sources_[f.sourceId];
                m.levelDb = std::max({m.levelDb, toDb(f.peak[0]), toDb(f.peak[1])});
            } else if constexpr (std::is_same_v<T, fb::SourcePosition>) {
                positions_[{f.sourceId, f.channel}] = f.position;
            } else if constexpr (std::is_same_v<T, fb::Clock>) {
                clock_ = f.state;
                hasClock_ = true;
            } else if constexpr (std::is_same_v<T, fb::Xrun>) {
                xruns_ = f.total;
            } else if constexpr (std::is_same_v<T, fb::CommandDropped>) {
                droppedCommands_ = f.total;
            }
        },
        feedback);
}

void FeedbackState::decay(double seconds) {
    if (!(seconds > 0.0)) return;
    const float fall = kMeterFallDbPerSecond * static_cast<float>(seconds);
    for (auto* meters : {&speakers_, &sources_})
        for (auto& [id, m] : *meters) m.levelDb = std::max(kMeterFloorDb, m.levelDb - fall);
}

float FeedbackState::level(const std::map<int, Meter>& meters, int id) {
    const auto it = meters.find(id);
    return it == meters.end() ? kMeterFloorDb : it->second.levelDb;
}

float FeedbackState::speakerLevelDb(int speakerId) const {
    return level(speakers_, speakerId);
}

float FeedbackState::speakerGainReductionDb(int speakerId) const {
    const auto it = speakers_.find(speakerId);
    return it == speakers_.end() ? 0.0f : it->second.gainReductionDb;
}

float FeedbackState::sourceLevelDb(int sourceId) const {
    return level(sources_, sourceId);
}

std::optional<Position> FeedbackState::sourcePosition(int sourceId, int channel) const {
    const auto it = positions_.find({sourceId, channel});
    if (it == positions_.end()) return std::nullopt;
    return it->second;
}

}  // namespace spacer
