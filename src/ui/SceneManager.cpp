#include "ui/SceneManager.h"

#include "ui/TextPrompt.h"
#include "ui/logic/CueListEditing.h"
#include "ui/logic/SceneEditing.h"

namespace spacer {

namespace {
const juce::Colour kBackground(0xff16181c);
const juce::Colour kText(0xffd8dce2);
const juce::Colour kDim(0xff8a919b);
const juce::Colour kLive(0xff4caf50);
const juce::Colour kModified(0xffffb74d);
const juce::Colour kSelected(0xff2a3f5c);
}  // namespace

SceneManager::SceneManager(AppContext& context) : context_(context) {
    title_.setText("Scenes", juce::dontSendNotification);
    title_.setFont(juce::FontOptions(13.0f));
    title_.setColour(juce::Label::textColourId, kText);
    liveLabel_.setFont(juce::FontOptions(12.0f));
    addAndMakeVisible(title_);
    addAndMakeVisible(liveLabel_);

    list_.setRowHeight(22);
    list_.setColour(juce::ListBox::backgroundColourId, kBackground);
    list_.setWantsKeyboardFocus(false);  // keep space = Go on the main window
    addAndMakeVisible(list_);

    timeLabel_.setText("Time (s)", juce::dontSendNotification);
    timeLabel_.setColour(juce::Label::textColourId, kDim);
    timeLabel_.setFont(juce::FontOptions(12.0f));
    timeEditor_.setText("2", false);
    timeEditor_.setTooltip("Transition time of Recall and Revert, seconds");
    restartToggle_.setColour(juce::ToggleButton::textColourId, kText);
    restartToggle_.setWantsKeyboardFocus(false);
    for (auto* c : std::initializer_list<juce::Component*>{&timeLabel_, &timeEditor_, &restartToggle_})
        addAndMakeVisible(c);

    recallButton_.onClick = [this] { recallSelected(); };
    revertButton_.onClick = [this] { revert(); };
    updateButton_.onClick = [this] { updateSelected(); };
    newButton_.onClick = [this] { createScene(); };
    renameButton_.onClick = [this] { renameSelected(); };
    deleteButton_.onClick = [this] { deleteSelected(); };
    recallButton_.setTooltip("Recall the selected scene (double-click a scene)");
    revertButton_.setTooltip("Recall the scene the show is in, discarding live changes");
    updateButton_.setTooltip("Write the live state into the selected scene (undoable)");
    newButton_.setTooltip("Store the live state as a new scene");
    for (auto* b :
         {&recallButton_, &revertButton_, &updateButton_, &newButton_, &renameButton_, &deleteButton_}) {
        b->setWantsKeyboardFocus(false);
        addAndMakeVisible(b);
    }

    seenRecalls_ = context_.cues().recallCount();
    liveScene_ = context_.cues().lastRecalledScene();
    context_.addListener(this);
    refresh();
}

SceneManager::~SceneManager() {
    context_.removeListener(this);
}

void SceneManager::paint(juce::Graphics& g) {
    g.fillAll(kBackground);
}

void SceneManager::resized() {
    auto area = getLocalBounds().reduced(6);
    auto head = area.removeFromTop(22);
    title_.setBounds(head.removeFromLeft(60));
    liveLabel_.setBounds(head);
    area.removeFromTop(4);

    auto buttons2 = area.removeFromBottom(26);
    area.removeFromBottom(4);
    auto buttons1 = area.removeFromBottom(26);
    area.removeFromBottom(4);
    auto timeRow = area.removeFromBottom(24);
    area.removeFromBottom(4);
    list_.setBounds(area);

    timeLabel_.setBounds(timeRow.removeFromLeft(56));
    timeEditor_.setBounds(timeRow.removeFromLeft(50));
    timeRow.removeFromLeft(10);
    restartToggle_.setBounds(timeRow);

    auto layoutRow = [](juce::Rectangle<int> row, std::initializer_list<juce::Component*> items) {
        const int w = row.getWidth() / static_cast<int>(items.size());
        for (auto* c : items) c->setBounds(row.removeFromLeft(w).reduced(2, 0));
    };
    layoutRow(buttons1, {&recallButton_, &revertButton_, &updateButton_});
    layoutRow(buttons2, {&newButton_, &renameButton_, &deleteButton_});
}

int SceneManager::getNumRows() {
    return static_cast<int>(context_.project().scenes.size());
}

void SceneManager::paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) {
    const auto& scenes = context_.project().scenes;
    if (row < 0 || row >= static_cast<int>(scenes.size())) return;
    const auto& scene = scenes[static_cast<std::size_t>(row)];
    if (selected) g.fillAll(kSelected);
    const bool live = scene.name == liveScene_;
    if (live) {
        g.setColour(modified_ ? kModified : kLive);
        g.fillEllipse(6.0f, static_cast<float>(height) * 0.5f - 4.0f, 8.0f, 8.0f);
    }
    g.setFont(juce::FontOptions(13.0f));
    g.setColour(kText);
    juce::String text(scene.name);
    if (live && modified_) text << "  (modified)";
    g.drawText(text, 22, 0, width - 70, height, juce::Justification::centredLeft, true);
    const auto uses = sceneedit::cuesUsing(context_.project(), scene.name).size();
    g.setColour(kDim);
    g.drawText(uses == 0 ? juce::String("unused")
                         : juce::String(static_cast<int>(uses)) + (uses == 1 ? " cue" : " cues"),
               width - 64, 0, 58, height, juce::Justification::centredRight);
}

void SceneManager::listBoxItemDoubleClicked(int row, const juce::MouseEvent&) {
    list_.selectRow(row);
    recallSelected();
}

void SceneManager::selectedRowsChanged(int lastRowSelected) {
    const auto& scenes = context_.project().scenes;
    selected_ = (lastRowSelected >= 0 && lastRowSelected < static_cast<int>(scenes.size()))
                    ? scenes[static_cast<std::size_t>(lastRowSelected)].name
                    : std::string{};
    const bool has = !selected_.empty();
    for (auto* b : {&recallButton_, &updateButton_, &renameButton_, &deleteButton_}) b->setEnabled(has);
}

void SceneManager::projectChanged(AppContext::Change change) {
    if (change == AppContext::Change::Loaded) {
        selected_.clear();
        liveScene_ = context_.cues().lastRecalledScene();
        seenRecalls_ = context_.cues().recallCount();
    }
    refresh();
}

void SceneManager::refresh() {
    if (context_.cues().recallCount() != seenRecalls_) {
        seenRecalls_ = context_.cues().recallCount();
        liveScene_ = context_.cues().lastRecalledScene();
    }
    const auto status = sceneedit::liveStatus(context_.project(), liveScene_);
    if (!status.known) liveScene_.clear();
    modified_ = status.known && status.modified;

    if (!status.known) {
        liveLabel_.setText("Live: no scene recalled", juce::dontSendNotification);
        liveLabel_.setColour(juce::Label::textColourId, kDim);
    } else {
        liveLabel_.setText("Live: " + juce::String(liveScene_) + (modified_ ? "  (modified)" : ""),
                           juce::dontSendNotification);
        liveLabel_.setColour(juce::Label::textColourId, modified_ ? kModified : kLive);
    }

    list_.updateContent();
    // Keep the selection by name; fall back to the live scene.
    int row = sceneedit::indexOfScene(context_.project(), selected_);
    if (row < 0) row = sceneedit::indexOfScene(context_.project(), liveScene_);
    if (row >= 0)
        list_.selectRow(row);
    else
        list_.deselectAllRows();
    selectedRowsChanged(list_.getSelectedRow());
    revertButton_.setEnabled(modified_);
    list_.repaint();
}

std::string SceneManager::selectedName() const {
    return selected_;
}

float SceneManager::transitionSeconds() {
    Cue probe;
    if (cueedit::setField(probe, cueedit::CueField::Transition, timeEditor_.getText().toStdString()))
        return probe.transitionSeconds;
    timeEditor_.setText("0", false);  // invalid text: instant recall, show it
    return 0.0f;
}

void SceneManager::recallSelected() {
    if (!selected_.empty())
        context_.recallScene(selected_, transitionSeconds(), restartToggle_.getToggleState());
}

void SceneManager::revert() {
    if (modified_ && !liveScene_.empty())
        context_.recallScene(liveScene_, transitionSeconds(), restartToggle_.getToggleState());
}

void SceneManager::updateSelected() {
    const std::string name = selected_;
    if (name.empty()) return;
    // Captures the live plug-in state first (SPEC §13), then stores the live state.
    if (!context_.updateScene(name)) return;
    liveScene_ = name;  // the live state now is exactly this scene
    refresh();
}

void SceneManager::createScene() {
    const auto suggestion = sceneedit::uniqueSceneName(context_.project(), "Scene");
    promptForText("New scene", "Store the live state as a scene named:", juce::String(suggestion),
                  [this](const juce::String& text) {
                      const std::string name = text.toStdString();
                      if (!context_.newScene(name)) {
                          juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                                                 "Could not create scene",
                                                                 "The name is empty or already used.");
                          return;
                      }
                      selected_ = name;
                      liveScene_ = name;
                      refresh();
                  });
}

void SceneManager::renameSelected() {
    const std::string from = selected_;
    if (from.empty()) return;
    promptForText("Rename scene", "Cues that recall it follow the new name.", juce::String(from),
                  [this, from](const juce::String& text) {
                      const std::string to = text.toStdString();
                      auto edit = edits::editShow(
                          context_.project(),
                          [from, to](Project& p) { return sceneedit::rename(p, from, to); },
                          "Rename Scene " + from);
                      if (!context_.perform(std::move(edit))) return;
                      if (liveScene_ == from) liveScene_ = to;
                      selected_ = to;
                      refresh();
                  });
}

void SceneManager::deleteSelected() {
    const std::string name = selected_;
    if (name.empty()) return;
    auto edit = edits::editShow(
        context_.project(), [name](Project& p) { return sceneedit::remove(p, name); },
        "Delete Scene " + name);
    if (!context_.perform(std::move(edit))) return;
    selected_.clear();
    refresh();
}

}  // namespace spacer
