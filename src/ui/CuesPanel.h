#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>
#include <string>

#include "app/AppContext.h"
#include "ui/SceneManager.h"
#include "ui/logic/CueListEditing.h"

namespace spacer {

/// Cues panel (SPEC §16 item 3), right side of the window: the armed cue
/// list as a table (current cue green, next cue amber), Go / Back / Jump,
/// editable name / scene / transition time / trigger / post-wait per cue, cue
/// and cue-list management, and the scene manager below. Every edit is an
/// undoable show edit (edits::editShow); the armed list follows edits through
/// AppContext (CueEngine::refreshArmedList). Logic: ui/logic/CueListEditing,
/// TriggerText, CueListView.
class CuesPanel final : public juce::Component,
                        private juce::TableListBoxModel,
                        private AppContext::Listener {
public:
    static constexpr int kPreferredWidth = 400;

    explicit CuesPanel(AppContext& context);
    ~CuesPanel() override;

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    enum Column { kNumber = 1, kName, kScene, kTime, kTrigger, kRestart, kPostWait };

    // TableListBoxModel
    int getNumRows() override;
    void paintRowBackground(juce::Graphics&, int row, int width, int height, bool selected) override;
    void paintCell(juce::Graphics&, int row, int columnId, int width, int height, bool selected) override;
    juce::Component* refreshComponentForCell(int row, int columnId, bool selected,
                                             juce::Component* existing) override;
    void cellDoubleClicked(int row, int columnId, const juce::MouseEvent&) override;
    void selectedRowsChanged(int lastRowSelected) override;

    // AppContext::Listener
    void projectChanged(AppContext::Change change) override;

    static std::optional<cueedit::CueField> fieldFor(int columnId);
    const CueList* viewedList() const;
    int viewedIndex() const;
    int currentIndex() const;
    int nextIndex() const;
    bool viewedIsArmed() const;
    void refresh();
    void updateButtons();
    void resolveViewed();

    /// Runs `modify` as one undoable show edit.
    bool performShow(const std::string& name, std::function<bool(Project&)> modify);
    void editCue(int row, cueedit::CueField field, const juce::String& text);
    void addCue();
    void duplicateCue();
    void removeCue();
    void moveCue(int delta);
    void go();
    void back();
    void jump();
    void arm();
    void newList();
    void renameList();
    void deleteList();

    class EditableCell;
    class SceneCell;
    class RestartCell;

    AppContext& context_;
    std::string viewedName_;

    juce::ComboBox listBox_;
    juce::TextButton armButton_{"Arm"};
    juce::TextButton newListButton_{"New"};
    juce::TextButton renameListButton_{"Rename"};
    juce::TextButton deleteListButton_{"Delete"};
    juce::Label currentLabel_;
    juce::Label nextLabel_;
    juce::TextButton backButton_{"Back"};
    juce::TextButton goButton_{"GO"};
    juce::TextButton jumpButton_{"Jump to"};
    juce::ToggleButton loopToggle_{"Loop"};
    juce::TableListBox table_;
    juce::TextButton addButton_{"Add"};
    juce::TextButton duplicateButton_{"Duplicate"};
    juce::TextButton removeButton_{"Remove"};
    juce::TextButton upButton_{"Up"};
    juce::TextButton downButton_{"Down"};
    SceneManager scenes_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CuesPanel)
};

}  // namespace spacer
