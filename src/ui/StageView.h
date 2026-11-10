#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <optional>
#include <vector>

#include "app/AppContext.h"
#include "ui/logic/PaintStats.h"
#include "ui/logic/StageScene.h"
#include "ui/logic/StageTransform.h"

namespace spacer {

/// The stage (SPEC §16 item 1): an orthographic 3D orbit view with Top / Side /
/// Front snaps; dragging empty space rotates. Perform mode drags sources and
/// clicks speakers to solo them; LayoutEdit mode drags and selects speakers.
/// The maths is in src/ui/logic; this class only draws and routes the mouse.
class StageView final : public juce::Component, private AppContext::Listener {
public:
    enum class Mode { Perform, LayoutEdit };

    StageView(AppContext& context, Mode mode);
    ~StageView() override;

    /// Jumps the camera to a snap; the view opens at StageSnap::ThreeD.
    void setSnap(StageSnap snap);
    std::optional<StageSnap> snap() const noexcept { return transform_.snap(); }

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
    void refreshSnapButtons();
    StagePoint toStage(juce::Point<float> p) const { return {p.x, p.y}; }
    juce::Point<float> toJuce(const Cartesian& c) const;

    void paintFloor(juce::Graphics& g) const;
    void paintMast(juce::Graphics& g) const;
    void paintListener(juce::Graphics& g) const;
    void paintGizmo(juce::Graphics& g) const;
    void paintTrajectories(juce::Graphics& g) const;
    void paintShareLines(juce::Graphics& g) const;
    void paintItems(juce::Graphics& g);
    void paintSpeaker(juce::Graphics& g, std::size_t index, int glowLimit);
    void paintSourcePoint(juce::Graphics& g, const StageSourcePoint& s, int cloudLimit);
    void paintStem(juce::Graphics& g, const Cartesian& position, juce::Colour colour, float cue) const;
    void paintCloud(juce::Graphics& g, const Cartesian& centre, float radiusMetres, int dots,
                    juce::Colour colour, float cue);

    AppContext& context_;
    Mode mode_;
    StageTransform transform_;
    StageScene scene_;
    struct SnapButton {
        StageSnap snap;
        juce::TextButton button;
    };
    std::array<SnapButton, 4> snapButtons_;
    bool orbiting_{false};
    bool orbitStarted_{false};
    juce::Point<float> lastOrbitPosition_;

    struct Drag {
        StageItem item;
        SourceSpec startSource;
        Speaker startSpeaker;
        bool moved{false};
    };
    std::optional<Drag> drag_;
    std::optional<StageItem> hover_;
    int selectedSpeaker_{-1};

    PaintStats paintStats_;
    bool showStats_{false};
    double animTime_{0.0};
    juce::RectangleList<float> dots_;
    std::vector<Cartesian> itemPositions_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StageView)
};

}  // namespace spacer
