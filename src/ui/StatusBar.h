#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>

#include "app/AppContext.h"
#include "ui/logic/StatusBarModel.h"

namespace spacer {

/// Status bar (SPEC §16.6): device, CPU %, xruns, latency, clock (bpm,
/// bar.beat, external source and lock), armed cue list, driver status and the
/// "Install virtual device..." offer. Content comes from statusbar::compose();
/// this component only draws it (about 4 Hz).
class StatusBar final : public juce::Component, private AppContext::Listener {
public:
    static constexpr int kHeight = 24;

    explicit StatusBar(AppContext& context);
    ~StatusBar() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    /// Called when the user asks to install the driver (the button also lives in the settings dialog).
    std::function<void()> onInstallDriver;

private:
    void feedbackUpdated() override;
    void update();
    statusbar::Inputs gatherInputs() const;

    AppContext& context_;
    juce::TooltipWindow tooltips_{this, 600};
    std::array<juce::Label, 7> labels_;  // device, cpu, xruns, latency, clock, cues, driver
    juce::TextButton installButton_{"Install virtual device..."};
    bool offerInstall_{false};
    int countdown_{0};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StatusBar)
};

}  // namespace spacer
