#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <optional>

#include "app/AppContext.h"
#include "ui/logic/StageScene.h"
#include "ui/logic/StageTransform.h"

namespace spacer {

/// The stage (SPEC §16 item 1): top-down or side view of the layout and the
/// sources. Speakers show their output meter as a ring; trajectories are
/// faint paths. Perform mode: drag sources (Manual position or motion
/// centre), click a speaker to solo pink noise on it. LayoutEdit mode (the
/// layout editor's stage): drag speakers, click to select.
///
/// The maths (transforms, hit-testing, drag rules) is in src/ui/logic and
/// unit-tested; this class only draws and routes mouse events.
class StageView final : public juce::Component, private AppContext::Listener {
public:
    enum class Mode { Perform, LayoutEdit };

    StageView(AppContext& context, Mode mode);
    ~StageView() override;

    /// Top or side view (SPEC §16: "toggle side view").
    void setProjection(StageProjection projection);
    StageProjection projection() const noexcept { return transform_.view(); }

    /// LayoutEdit: the selected speaker (highlighted); -1 = none.
    void setSelectedSpeaker(int speakerId);
    int selectedSpeaker() const noexcept { return selectedSpeaker_; }
    /// LayoutEdit: called when the user clicks a speaker.
    std::function<void(int speakerId)> onSpeakerClicked;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    void mouseMove(const juce::MouseEvent& e) override;

private:
    void projectChanged(AppContext::Change change) override;
    void feedbackUpdated() override;

    void refreshScene(bool refit);
    void toggleView();
    StagePoint toStage(juce::Point<float> p) const { return {p.x, p.y}; }
    juce::Point<float> toJuce(const Cartesian& c) const;

    void paintGrid(juce::Graphics& g) const;
    void paintSpeakers(juce::Graphics& g) const;
    void paintTrajectories(juce::Graphics& g) const;
    void paintSources(juce::Graphics& g) const;

    AppContext& context_;
    Mode mode_;
    StageTransform transform_;
    StageScene scene_;
    juce::TextButton viewButton_;

    struct Drag {
        StageItem item;
        SourceSpec startSource;
        Speaker startSpeaker;
        bool moved{false};
    };
    std::optional<Drag> drag_;
    std::optional<StageItem> hover_;
    int selectedSpeaker_{-1};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StageView)
};

}  // namespace spacer
