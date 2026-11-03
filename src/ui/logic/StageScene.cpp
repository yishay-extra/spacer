#include "ui/logic/StageScene.h"

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

    sources_.clear();
    for (const auto& s : project.sources) {
        const auto model = modelPoints(s, project.layout);
        for (int ch = 0; ch < static_cast<int>(model.size()); ++ch) {
            const auto rendered = feedback.sourcePosition(s.id, ch);
            const Position p = rendered ? *rendered : model[static_cast<std::size_t>(ch)];
            sources_.push_back({s.id, ch, s.name, p.toCartesian(), s.mute});
        }
    }
}

std::vector<StageItem> StageScene::items() const {
    std::vector<StageItem> out;
    out.reserve(sources_.size() + speakers_.size());
    for (const auto& s : sources_) out.push_back({StageItem::Kind::Source, s.id, s.channel, s.position});
    for (const auto& s : speakers_) out.push_back({StageItem::Kind::Speaker, s.id, 0, s.position});
    return out;
}

}  // namespace spacer
