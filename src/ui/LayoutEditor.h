#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/AppContext.h"
#include "ui/InsertSlotsView.h"
#include "ui/StageView.h"
#include "ui/logic/LayoutEditing.h"

namespace spacer {

/// Layout editor (SPEC §16 item 4): speaker table + stage edit, presets,
/// computed compensation shown read-only, output channel assignment and a
/// per-speaker test. Every change is an undoable layout edit
/// (edits::setLayout) and rebuilds the engine graph. Below the table, the
/// inserts of the selected speaker (SPEC §13, WP11e): the same view as the
/// source strip, with edits from ui/logic/SpeakerChainEditing.
class LayoutEditor final : public juce::Component,
                           private juce::TableListBoxModel,
                           private AppContext::Listener {
public:
    explicit LayoutEditor(AppContext& context);
    ~LayoutEditor() override;

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    enum Column {
        kId = 1,
        kName,
        kAzimuth,
        kElevation,
        kDistance,
        kTrim,
        kDelay,
        kPolarity,
        kOutput,
        kLfe,
        kCompDelay,
        kCompGain,
        kEffDelay,
        kEffGain,
        kTest,
    };

    // TableListBoxModel
    int getNumRows() override;
    void paintRowBackground(juce::Graphics&, int row, int width, int height, bool selected) override;
    void paintCell(juce::Graphics&, int row, int columnId, int width, int height, bool selected) override;
    juce::Component* refreshComponentForCell(int row, int columnId, bool selected,
                                             juce::Component* existing) override;
    void selectedRowsChanged(int lastRowSelected) override;

    // AppContext::Listener
    void projectChanged(AppContext::Change change) override;
    void feedbackUpdated() override;

    static std::optional<layoutedit::SpeakerField> fieldFor(int columnId);
    void editField(int speakerId, layoutedit::SpeakerField field, const juce::String& text);
    void applyLayout(Layout layout, const std::string& name);
    void refresh();
    void refreshInserts();
    int selectedSpeakerId() const;
    void selectSpeaker(int speakerId);

    void applyPreset();
    void addSpeaker();
    void duplicateSpeaker();
    void removeSpeaker();
    void assignOutputs();

    class EditableCell;
    class TestCell;

    AppContext& context_;
    juce::ComboBox presetBox_;
    juce::TextButton applyPresetButton_{"Apply preset"};
    juce::TextButton addButton_{"Add"};
    juce::TextButton duplicateButton_{"Duplicate"};
    juce::TextButton removeButton_{"Remove"};
    juce::TextButton assignButton_{"Assign outputs 1..N"};
    juce::Label nameLabel_;
    juce::TableListBox table_;
    juce::Label issuesLabel_;
    juce::Label insertsTitle_;
    InsertSlotsView inserts_;
    StageView stage_;
    std::vector<layoutedit::CompensationRow> rows_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LayoutEditor)
};

}  // namespace spacer
