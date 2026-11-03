#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <string>

#include "app/AppContext.h"

namespace spacer {

/// Scene manager (SPEC §11, §16 item 3): the project's scenes with recall /
/// update / new / rename / delete, and the "modified" marker of the scene the
/// show is in. All scene edits are undoable show edits (edits::editShow, logic
/// in ui/logic/SceneEditing); recall goes through AppContext::recallScene.
class SceneManager final : public juce::Component, private juce::ListBoxModel, private AppContext::Listener {
public:
    explicit SceneManager(AppContext& context);
    ~SceneManager() override;

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    // ListBoxModel
    int getNumRows() override;
    void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) override;
    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override;
    void selectedRowsChanged(int lastRowSelected) override;

    // AppContext::Listener
    void projectChanged(AppContext::Change change) override;

    void refresh();
    std::string selectedName() const;
    float transitionSeconds();
    void recallSelected();
    void revert();
    void updateSelected();
    void createScene();
    void renameSelected();
    void deleteSelected();

    AppContext& context_;
    juce::Label title_;
    juce::Label liveLabel_;
    juce::ListBox list_{"scenes", this};
    juce::Label timeLabel_;
    juce::TextEditor timeEditor_;
    juce::ToggleButton restartToggle_{"Restart motion"};
    juce::TextButton recallButton_{"Recall"};
    juce::TextButton revertButton_{"Revert"};
    juce::TextButton updateButton_{"Update"};
    juce::TextButton newButton_{"New"};
    juce::TextButton renameButton_{"Rename"};
    juce::TextButton deleteButton_{"Delete"};

    std::string selected_;
    std::string liveScene_;  // the scene the live state was last recalled from / stored as
    unsigned long long seenRecalls_{0};
    bool modified_{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SceneManager)
};

}  // namespace spacer
