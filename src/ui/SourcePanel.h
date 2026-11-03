#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/AppContext.h"
#include "ui/SourceStrip.h"

namespace spacer {

/// The Sources panel (SPEC §16 item 2): the source list with per-source level
/// meters and add / duplicate / remove, and below it the strip of the
/// selected source. The selection is AppContext's SourceSelection, shared
/// with the stage view and (later) OSC / MIDI.
class SourcePanel final : public juce::Component, private AppContext::Listener, private juce::ListBoxModel {
public:
    explicit SourcePanel(AppContext& context);
    ~SourcePanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    // AppContext::Listener
    void projectChanged(AppContext::Change change) override;
    void feedbackUpdated() override;

    // juce::ListBoxModel
    int getNumRows() override;
    void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) override;
    void selectedRowsChanged(int lastRowSelected) override;
    void deleteKeyPressed(int lastRowSelected) override;

    void showAddMenu();
    void duplicateSelected();
    void removeSelected();
    void syncSelection();
    void layoutStrip();

    AppContext& context_;
    juce::TextButton addButton_{"+ Add"};
    juce::TextButton duplicateButton_{"Duplicate"};
    juce::TextButton removeButton_{"Remove"};
    juce::ListBox list_;
    juce::Viewport viewport_;
    SourceStrip strip_;
    bool syncing_{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SourcePanel)
};

}  // namespace spacer
