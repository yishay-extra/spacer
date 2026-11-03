#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

#include "app/AppContext.h"
#include "ui/LayoutEditor.h"
#include "ui/StageView.h"

namespace spacer {

/// Root component of the main window (SPEC §16): toolbar, Stage and Layout
/// tabs, a one-line status. Keyboard: space = Go, Esc = panic (toggle),
/// Cmd+Z = undo, Shift+Cmd+Z = redo. The source, cue, settings panels and the
/// full status bar arrive with WP11b-d.
class MainComponent final : public juce::Component, private AppContext::Listener {
public:
    static constexpr int kMinWidth = 1100;  // SPEC §16
    static constexpr int kMinHeight = 700;

    explicit MainComponent(AppContext& context);
    ~MainComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    void projectChanged(AppContext::Change change) override;
    void feedbackUpdated() override;
    void updateButtons();
    void openProject();
    void saveProject(bool saveAs);
    void installVirtualDevice();

    AppContext& context_;
    juce::TextButton openButton_{"Open..."};
    juce::TextButton saveButton_{"Save"};
    juce::TextButton saveAsButton_{"Save As..."};
    juce::TextButton undoButton_{"Undo"};
    juce::TextButton redoButton_{"Redo"};
    juce::TextButton backButton_{"Back"};
    juce::TextButton goButton_{"GO"};
    juce::TextButton panicButton_{"Panic"};
    juce::TextButton installDriverButton_{"Install virtual device..."};
    juce::Label statusLabel_;
    juce::TabbedComponent tabs_{juce::TabbedButtonBar::TabsAtTop};
    std::unique_ptr<juce::FileChooser> chooser_;
    int statusCountdown_{0};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};

}  // namespace spacer
