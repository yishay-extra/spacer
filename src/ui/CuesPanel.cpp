#include "ui/CuesPanel.h"

#include <algorithm>

#include "ui/TextPrompt.h"
#include "ui/logic/CueListView.h"
#include "ui/logic/TriggerText.h"

namespace spacer {

namespace {
const juce::Colour kBackground(0xff16181c);
const juce::Colour kText(0xffd8dce2);
const juce::Colour kDim(0xff8a919b);
const juce::Colour kRowA(0xff1a1d21);
const juce::Colour kRowB(0xff1e2126);
const juce::Colour kCurrent(0xff1f5c2e);
const juce::Colour kNext(0xff5c4a1a);
const juce::Colour kBoth(0xff2f5a3a);
const juce::Colour kSelectedBar(0xff4a8fe0);
const juce::Colour kMissing(0xffe57373);
}  // namespace

// ---- cells ----------------------------------------------------------------------

class CuesPanel::EditableCell final : public juce::Label {
public:
    explicit EditableCell(CuesPanel& owner) : owner_(owner) {
        setEditable(false, true, false);
        setColour(juce::Label::textColourId, kText);
        setFont(juce::FontOptions(12.0f));
        onTextChange = [this] {
            if (field_) owner_.editCue(row_, *field_, getText());
        };
    }
    void bind(int row, cueedit::CueField field, const juce::String& text, bool warn) {
        row_ = row;
        field_ = field;
        setColour(juce::Label::textColourId, warn ? kMissing : kText);
        setText(text, juce::dontSendNotification);
    }
    void mouseDown(const juce::MouseEvent& e) override {
        if (auto* table = findParentComponentOfClass<juce::TableListBox>())
            table->selectRowsBasedOnModifierKeys(table->getRowNumberOfComponent(this), e.mods, false);
        juce::Label::mouseDown(e);
    }

private:
    CuesPanel& owner_;
    int row_{0};
    std::optional<cueedit::CueField> field_;
};

/// Scene choice for a cue. A cue whose scene is gone shows it in red.
class CuesPanel::SceneCell final : public juce::ComboBox {
public:
    explicit SceneCell(CuesPanel& owner) : owner_(owner) {
        setWantsKeyboardFocus(false);
        onChange = [this] {
            if (!binding_) owner_.editCue(row_, cueedit::CueField::Scene, getText());
        };
    }
    void bind(int row, const Cue& cue, const Project& project) {
        binding_ = true;
        row_ = row;
        clear(juce::dontSendNotification);
        int id = 1;
        int selected = 0;
        for (const auto& s : project.scenes) {
            addItem(juce::String(s.name), id);
            if (s.name == cue.sceneName) selected = id;
            ++id;
        }
        if (selected == 0) {  // missing scene: keep it visible
            addItem(juce::String(cue.sceneName) + " (missing)", id);
            selected = id;
        }
        setSelectedId(selected, juce::dontSendNotification);
        setColour(juce::ComboBox::textColourId,
                  selected == id && selected > static_cast<int>(project.scenes.size()) ? kMissing : kText);
        binding_ = false;
    }

private:
    CuesPanel& owner_;
    int row_{0};
    bool binding_{false};
};

class CuesPanel::RestartCell final : public juce::ToggleButton {
public:
    explicit RestartCell(CuesPanel& owner) : owner_(owner) {
        setWantsKeyboardFocus(false);
        setTooltip("Restart motion phase on recall");
        onClick = [this] {
            owner_.editCue(row_, cueedit::CueField::RestartPhase, getToggleState() ? "yes" : "no");
        };
    }
    void bind(int row, bool on) {
        row_ = row;
        setToggleState(on, juce::dontSendNotification);
    }

private:
    CuesPanel& owner_;
    int row_{0};
};

// ---- panel --------------------------------------------------------------------------

CuesPanel::CuesPanel(AppContext& context) : context_(context), table_("cues", this), scenes_(context) {
    for (auto* b :
         {&armButton_, &newListButton_, &renameListButton_, &deleteListButton_, &backButton_, &goButton_,
          &jumpButton_, &addButton_, &duplicateButton_, &removeButton_, &upButton_, &downButton_}) {
        b->setWantsKeyboardFocus(false);  // keep space = Go on the main window
        addAndMakeVisible(b);
    }
    loopToggle_.setWantsKeyboardFocus(false);
    loopToggle_.setColour(juce::ToggleButton::textColourId, kText);
    addAndMakeVisible(loopToggle_);
    listBox_.setWantsKeyboardFocus(false);
    listBox_.setTooltip("Cue list shown. The armed list is the one Go fires.");
    listBox_.onChange = [this] {
        const int index = listBox_.getSelectedId() - 1;
        if (index >= 0 && index < static_cast<int>(context_.project().cueLists.size())) {
            viewedName_ = context_.project().cueLists[static_cast<std::size_t>(index)].name;
            refresh();
        }
    };
    addAndMakeVisible(listBox_);

    for (auto* l : {&currentLabel_, &nextLabel_}) {
        l->setFont(juce::FontOptions(13.0f));
        l->setColour(juce::Label::textColourId, kText);
        addAndMakeVisible(l);
    }

    armButton_.onClick = [this] { arm(); };
    newListButton_.onClick = [this] { newList(); };
    renameListButton_.onClick = [this] { renameList(); };
    deleteListButton_.onClick = [this] { deleteList(); };
    backButton_.onClick = [this] { back(); };
    goButton_.onClick = [this] { go(); };
    jumpButton_.onClick = [this] { jump(); };
    addButton_.onClick = [this] { addCue(); };
    duplicateButton_.onClick = [this] { duplicateCue(); };
    removeButton_.onClick = [this] { removeCue(); };
    upButton_.onClick = [this] { moveCue(-1); };
    downButton_.onClick = [this] { moveCue(1); };
    loopToggle_.onClick = [this] {
        const int index = viewedIndex();
        if (index < 0) return;
        const bool loop = loopToggle_.getToggleState();
        performShow("Set Loop", [index, loop](Project& p) {
            auto& list = p.cueLists[static_cast<std::size_t>(index)];
            if (list.loop == loop) return false;
            list.loop = loop;
            return true;
        });
    };
    goButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2e7d32));
    goButton_.setTooltip("Fire the next cue (space)");
    backButton_.setTooltip("Fire the previous cue");
    jumpButton_.setTooltip("Fire the selected cue now (also: double-click its number)");
    armButton_.setTooltip("Make this the armed list (position resets)");

    auto& header = table_.getHeader();
    header.addColumn("#", kNumber, 30, 30, 30, juce::TableHeaderComponent::notResizable);
    header.addColumn("Name", kName, 84, 40, 300, juce::TableHeaderComponent::defaultFlags);
    header.addColumn("Scene", kScene, 84, 40, 300, juce::TableHeaderComponent::defaultFlags);
    header.addColumn("Time", kTime, 40, 30, 100, juce::TableHeaderComponent::defaultFlags);
    header.addColumn("Trigger", kTrigger, 108, 50, 300, juce::TableHeaderComponent::defaultFlags);
    header.addColumn("R", kRestart, 22, 22, 22, juce::TableHeaderComponent::notResizable);
    header.addColumn("Wait", kPostWait, 40, 30, 100, juce::TableHeaderComponent::defaultFlags);
    header.setPopupMenuActive(false);
    table_.setHeaderHeight(22);
    table_.setRowHeight(24);
    table_.setColour(juce::ListBox::backgroundColourId, kBackground);
    table_.setWantsKeyboardFocus(false);
    addAndMakeVisible(table_);
    addAndMakeVisible(scenes_);

    context_.addListener(this);
    resolveViewed();
    refresh();
}

CuesPanel::~CuesPanel() {
    context_.removeListener(this);
}

void CuesPanel::paint(juce::Graphics& g) {
    g.fillAll(kBackground);
    g.setColour(juce::Colour(0xff2a2f36));
    g.drawVerticalLine(0, 0.0f, static_cast<float>(getHeight()));
}

void CuesPanel::resized() {
    auto area = getLocalBounds().reduced(6);
    area.removeFromLeft(2);

    auto row = area.removeFromTop(24);
    deleteListButton_.setBounds(row.removeFromRight(52));
    row.removeFromRight(3);
    renameListButton_.setBounds(row.removeFromRight(58));
    row.removeFromRight(3);
    newListButton_.setBounds(row.removeFromRight(40));
    row.removeFromRight(3);
    armButton_.setBounds(row.removeFromRight(42));
    row.removeFromRight(3);
    listBox_.setBounds(row);
    area.removeFromTop(4);

    currentLabel_.setBounds(area.removeFromTop(20));
    nextLabel_.setBounds(area.removeFromTop(20));
    area.removeFromTop(4);

    auto transport = area.removeFromTop(34);
    loopToggle_.setBounds(transport.removeFromRight(56));
    jumpButton_.setBounds(transport.removeFromRight(70).reduced(0, 4));
    transport.removeFromRight(6);
    backButton_.setBounds(transport.removeFromLeft(64));
    transport.removeFromLeft(4);
    goButton_.setBounds(transport.removeFromLeft(110));
    area.removeFromTop(4);

    // Scene manager gets the lower part; the cue table the rest.
    scenes_.setBounds(area.removeFromBottom(std::clamp(area.getHeight() * 2 / 5, 200, 300)));
    area.removeFromBottom(4);
    auto edit = area.removeFromBottom(26);
    area.removeFromBottom(4);
    const int w = edit.getWidth() / 5;
    for (auto* b : {&addButton_, &duplicateButton_, &removeButton_, &upButton_, &downButton_})
        b->setBounds(edit.removeFromLeft(w).reduced(2, 0));
    table_.setBounds(area);
}

// ---- state -----------------------------------------------------------------------------

const CueList* CuesPanel::viewedList() const {
    const int i = viewedIndex();
    return i < 0 ? nullptr : &context_.project().cueLists[static_cast<std::size_t>(i)];
}

int CuesPanel::viewedIndex() const {
    return cueedit::indexOfCueList(context_.project(), viewedName_);
}

bool CuesPanel::viewedIsArmed() const {
    return context_.cues().armed() && context_.cues().armedName() == viewedName_;
}

int CuesPanel::currentIndex() const {
    return viewedIsArmed() ? context_.cues().sequencer().current() : -1;
}

int CuesPanel::nextIndex() const {
    return viewedIsArmed() ? context_.cues().sequencer().next() : -1;
}

void CuesPanel::resolveViewed() {
    const auto& p = context_.project();
    if (viewedIndex() >= 0) return;
    if (context_.cues().armed() && cueedit::indexOfCueList(p, context_.cues().armedName()) >= 0)
        viewedName_ = context_.cues().armedName();
    else
        viewedName_ = p.cueLists.empty() ? std::string{} : p.cueLists.front().name;
}

void CuesPanel::projectChanged(AppContext::Change change) {
    if (change == AppContext::Change::Loaded) viewedName_.clear();
    resolveViewed();
    refresh();
}

void CuesPanel::refresh() {
    const auto& p = context_.project();
    listBox_.clear(juce::dontSendNotification);
    for (std::size_t i = 0; i < p.cueLists.size(); ++i) {
        const auto& l = p.cueLists[i];
        const bool armed = context_.cues().armed() && context_.cues().armedName() == l.name;
        listBox_.addItem(juce::String(l.name) + (armed ? "  (armed)" : ""), static_cast<int>(i) + 1);
    }
    const int viewed = viewedIndex();
    if (viewed >= 0)
        listBox_.setSelectedId(viewed + 1, juce::dontSendNotification);
    else
        listBox_.setTextWhenNothingSelected("No cue lists");

    const CueList* list = viewedList();
    const bool armed = viewedIsArmed();
    if (list == nullptr) {
        currentLabel_.setText("No cue list", juce::dontSendNotification);
        nextLabel_.setText("Create one with New", juce::dontSendNotification);
    } else if (armed) {
        currentLabel_.setText(juce::String(cueview::currentText(*list, currentIndex())),
                              juce::dontSendNotification);
        nextLabel_.setText(juce::String(cueview::nextText(*list, nextIndex())), juce::dontSendNotification);
    } else {
        currentLabel_.setText(context_.cues().armed()
                                  ? "Armed list: " + juce::String(context_.cues().armedName())
                                  : juce::String("No list armed"),
                              juce::dontSendNotification);
        nextLabel_.setText("Press Arm to use this list", juce::dontSendNotification);
    }
    currentLabel_.setColour(juce::Label::textColourId, armed ? juce::Colour(0xff7fd18b) : kDim);
    nextLabel_.setColour(juce::Label::textColourId, armed ? juce::Colour(0xffffcc80) : kDim);

    loopToggle_.setToggleState(list != nullptr && list->loop, juce::dontSendNotification);
    table_.updateContent();
    table_.repaint();
    updateButtons();
}

void CuesPanel::updateButtons() {
    const CueList* list = viewedList();
    const bool armed = viewedIsArmed();
    const bool has = list != nullptr;
    armButton_.setEnabled(has && !armed);
    renameListButton_.setEnabled(has);
    deleteListButton_.setEnabled(has);
    loopToggle_.setEnabled(has);
    addButton_.setEnabled(has);
    const int selected = table_.getSelectedRow();
    const bool rowSelected = has && selected >= 0;
    goButton_.setEnabled(has);
    backButton_.setEnabled(armed);
    jumpButton_.setEnabled(rowSelected);
    duplicateButton_.setEnabled(rowSelected);
    removeButton_.setEnabled(rowSelected);
    upButton_.setEnabled(rowSelected && selected > 0);
    downButton_.setEnabled(rowSelected && selected + 1 < static_cast<int>(list->cues.size()));
}

void CuesPanel::selectedRowsChanged(int) {
    updateButtons();
}

// ---- table model -------------------------------------------------------------------------

std::optional<cueedit::CueField> CuesPanel::fieldFor(int columnId) {
    using F = cueedit::CueField;
    switch (columnId) {
        case kName:
            return F::Name;
        case kScene:
            return F::Scene;
        case kTime:
            return F::Transition;
        case kTrigger:
            return F::Trigger;
        case kRestart:
            return F::RestartPhase;
        case kPostWait:
            return F::PostWait;
        default:
            return std::nullopt;
    }
}

int CuesPanel::getNumRows() {
    const CueList* l = viewedList();
    return l == nullptr ? 0 : static_cast<int>(l->cues.size());
}

void CuesPanel::paintRowBackground(juce::Graphics& g, int row, int width, int height, bool selected) {
    const CueList* l = viewedList();
    if (l == nullptr || row >= static_cast<int>(l->cues.size())) return;
    cueview::Row state;
    state.index = row;
    state.current = row == currentIndex();
    state.next = row == nextIndex();
    switch (cueview::highlight(state)) {
        case cueview::Highlight::Current:
            g.fillAll(kCurrent);
            break;
        case cueview::Highlight::Next:
            g.fillAll(kNext);
            break;
        case cueview::Highlight::CurrentAndNext:
            g.fillAll(kBoth);
            break;
        case cueview::Highlight::None:
            g.fillAll(row % 2 == 0 ? kRowA : kRowB);
            break;
    }
    if (selected) {
        g.setColour(kSelectedBar);
        g.fillRect(0, 0, 3, height);
        g.drawRect(0, 0, width, height, 1);
    }
}

void CuesPanel::paintCell(juce::Graphics& g, int row, int columnId, int width, int height, bool) {
    if (columnId != kNumber) return;
    g.setFont(juce::FontOptions(12.0f));
    g.setColour(kDim);
    g.drawText(juce::String(row + 1), 4, 0, width - 6, height, juce::Justification::centredRight);
    // Marker: filled dot = current cue, ring = next cue.
    const float cy = static_cast<float>(height) * 0.5f;
    if (row == currentIndex()) {
        g.setColour(juce::Colour(0xff7fd18b));
        g.fillEllipse(5.0f, cy - 3.5f, 7.0f, 7.0f);
    } else if (row == nextIndex()) {
        g.setColour(juce::Colour(0xffffcc80));
        g.drawEllipse(5.0f, cy - 3.5f, 7.0f, 7.0f, 1.5f);
    }
}

juce::Component* CuesPanel::refreshComponentForCell(int row, int columnId, bool, juce::Component* existing) {
    const CueList* l = viewedList();
    const auto field = fieldFor(columnId);
    if (l == nullptr || !field || row < 0 || row >= static_cast<int>(l->cues.size())) {
        delete existing;
        return nullptr;
    }
    const Cue& cue = l->cues[static_cast<std::size_t>(row)];
    if (columnId == kScene) {
        auto* cell = dynamic_cast<SceneCell*>(existing);
        if (cell == nullptr) {
            delete existing;
            cell = new SceneCell(*this);
        }
        cell->bind(row, cue, context_.project());
        return cell;
    }
    if (columnId == kRestart) {
        auto* cell = dynamic_cast<RestartCell*>(existing);
        if (cell == nullptr) {
            delete existing;
            cell = new RestartCell(*this);
        }
        cell->bind(row, cue.restartMotionPhase);
        return cell;
    }
    auto* cell = dynamic_cast<EditableCell*>(existing);
    if (cell == nullptr) {
        delete existing;
        cell = new EditableCell(*this);
    }
    cell->bind(row, *field, juce::String(cueedit::formatField(cue, *field)), false);
    return cell;
}

void CuesPanel::cellDoubleClicked(int row, int columnId, const juce::MouseEvent&) {
    if (columnId == kNumber) {
        table_.selectRow(row);
        jump();
    }
}

// ---- actions ---------------------------------------------------------------------------------

bool CuesPanel::performShow(const std::string& name, std::function<bool(Project&)> modify) {
    auto edit = edits::editShow(context_.project(), modify, name);
    return context_.perform(std::move(edit));
}

void CuesPanel::editCue(int row, cueedit::CueField field, const juce::String& text) {
    const int index = viewedIndex();
    if (index < 0) return;
    const std::string value = text.toStdString();
    const bool ok = performShow("Edit Cue", [index, row, field, value](Project& p) {
        return cueedit::editCueField(p.cueLists[static_cast<std::size_t>(index)], row, field, value);
    });
    if (!ok) refresh();  // invalid or unchanged: show the model value again
}

void CuesPanel::addCue() {
    const int index = viewedIndex();
    if (index < 0) return;
    const int after = table_.getSelectedRow() >= 0 ? table_.getSelectedRow() : getNumRows() - 1;
    // New cue recalls the scene the show is in, else the first scene.
    const std::string scene = context_.cues().lastRecalledScene();
    int added = -1;
    performShow("Add Cue", [&, index, after, scene](Project& p) {
        auto& list = p.cueLists[static_cast<std::size_t>(index)];
        const auto at = cueedit::addCue(list, cueedit::makeCue(p, list, scene), after);
        if (at) added = *at;
        return at.has_value();
    });
    if (added >= 0) table_.selectRow(added);
}

void CuesPanel::duplicateCue() {
    const int index = viewedIndex();
    const int row = table_.getSelectedRow();
    if (index < 0 || row < 0) return;
    int added = -1;
    performShow("Duplicate Cue", [&, index, row](Project& p) {
        const auto at = cueedit::duplicateCue(p.cueLists[static_cast<std::size_t>(index)], row);
        if (at) added = *at;
        return at.has_value();
    });
    if (added >= 0) table_.selectRow(added);
}

void CuesPanel::removeCue() {
    const int index = viewedIndex();
    const int row = table_.getSelectedRow();
    if (index < 0 || row < 0) return;
    performShow("Remove Cue", [index, row](Project& p) {
        return cueedit::removeCue(p.cueLists[static_cast<std::size_t>(index)], row);
    });
    const int n = getNumRows();
    if (n > 0) table_.selectRow(std::min(row, n - 1));
}

void CuesPanel::moveCue(int delta) {
    const int index = viewedIndex();
    const int row = table_.getSelectedRow();
    if (index < 0 || row < 0) return;
    if (performShow("Move Cue", [index, row, delta](Project& p) {
            return cueedit::moveCue(p.cueLists[static_cast<std::size_t>(index)], row, row + delta);
        }))
        table_.selectRow(row + delta);
}

void CuesPanel::go() {
    if (!context_.cues().armed() && viewedList() != nullptr) context_.armCueList(viewedName_);
    context_.go();
}

void CuesPanel::back() {
    context_.back();
}

void CuesPanel::jump() {
    const int row = table_.getSelectedRow();
    if (row < 0 || !viewedIsArmed()) {
        if (row >= 0 && viewedList() != nullptr) {
            context_.armCueList(viewedName_);
            context_.jumpToCue(row);
        }
        return;
    }
    context_.jumpToCue(row);
}

void CuesPanel::arm() {
    if (viewedList() != nullptr) context_.armCueList(viewedName_);
}

void CuesPanel::newList() {
    const auto suggestion = cueedit::uniqueCueListName(context_.project(), "Cue list");
    promptForText("New cue list", "Name:", juce::String(suggestion), [this](const juce::String& text) {
        const std::string name = text.toStdString();
        if (performShow("New Cue List",
                        [name](Project& p) { return cueedit::addCueList(p, name).has_value(); })) {
            viewedName_ = name;
            refresh();
        } else {
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                                   "Could not create cue list",
                                                   "The name is empty or already used.");
        }
    });
}

void CuesPanel::renameList() {
    const int index = viewedIndex();
    if (index < 0) return;
    promptForText(
        "Rename cue list", "Name:", juce::String(viewedName_), [this, index](const juce::String& text) {
            const std::string name = text.toStdString();
            if (performShow("Rename Cue List",
                            [index, name](Project& p) { return cueedit::renameCueList(p, index, name); })) {
                viewedName_ = name;
                refresh();
            }
        });
}

void CuesPanel::deleteList() {
    const int index = viewedIndex();
    if (index < 0) return;
    performShow("Delete Cue List", [index](Project& p) { return cueedit::removeCueList(p, index); });
    viewedName_.clear();
    resolveViewed();
    refresh();
}

}  // namespace spacer
