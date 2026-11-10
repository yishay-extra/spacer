#include "ui/logic/StageScene.h"

#include <algorithm>

#include "ui/logic/SourceGeometry.h"
#include "ui/logic/TrajectoryPreview.h"

namespace spacer {

void StageScene::setProject(const Project& project) {
    trajectories_.clear();
    TrajectoryPreviewOptions options;
    options.bpm = project.clock.bpm;
    for (const auto& s : project.sources) {
        const int n = (s.stereo && s.linkedPoints) ? 2 : 1;
        for (int ch = 0; ch < n; ++ch) {
            auto points = previewTrajectory(motionForChannel(s, ch), project.layout, options);
            if (!points.empty()) trajectories_.push_back({s.id, ch, std::move(points)});
        }
    }
}

void StageScene::update(const Project& project, const FeedbackState& feedback) {
    speakers_.clear();
    for (const auto& sp : project.layout.speakers)
        speakers_.push_back({sp.id, sp.name, sp.position.toCartesian(), sp.lfe,
                             feedback.speakerLevelDb(sp.id), feedback.speakerGainReductionDb(sp.id)});

    if (!sharesPrepared_ || !(project.layout == sharesLayout_)) {
        shares_.prepare(project.layout);
        sharesLayout_ = project.layout;
        sharesPrepared_ = true;
    }

    sources_.clear();
    for (const auto& s : project.sources) {
        const auto model = modelPoints(s, project.layout);
        const float level =
            s.mute ? 0.0f : FeedbackState::meterFraction(feedback.sourceLevelDb(s.id));
        for (int ch = 0; ch < static_cast<int>(model.size()); ++ch) {
            const auto rendered = feedback.sourcePosition(s.id, ch);
            const Position p = rendered ? *rendered : model[static_cast<std::size_t>(ch)];
            StageSourcePoint point;
            point.id = s.id;
            point.channel = ch;
            point.name = s.name;
            point.position = p.toCartesian();
            point.mute = s.mute;
            point.colour = sourceColour(s.id);
            point.level = level;
            if (level > 0.0f) {
                const auto count = static_cast<std::size_t>(shares_.numSpeakers());
                point.hasShares = shares_.gainsFor(s, p, std::span<float>(point.shares.data(), count));
                for (std::size_t i = 0; i < point.shares.size(); ++i) {
                    point.shares[i] = point.hasShares ? point.shares[i] * level : 0.0f;
                    if (point.shares[i] < kMinDrawnShare) point.shares[i] = 0.0f;
                }
            }
            sources_.push_back(std::move(point));
        }
    }
}

int StageScene::loudPoints() const {
    return static_cast<int>(
        std::count_if(sources_.begin(), sources_.end(), [](const auto& s) { return s.level > 0.0f; }));
}

std::vector<StageItem> StageScene::items() const {
    std::vector<StageItem> out;
    out.reserve(sources_.size() + speakers_.size());
    for (const auto& s : sources_) out.push_back({StageItem::Kind::Source, s.id, s.channel, s.position});
    for (const auto& s : speakers_) out.push_back({StageItem::Kind::Speaker, s.id, 0, s.position});
    return out;
}

}  // namespace spacer
