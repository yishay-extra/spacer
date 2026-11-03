#include "ui/SourcePanel.h"

#include <algorithm>

#include "ui/logic/FeedbackState.h"
#include "ui/logic/SourceEditing.h"
#include "ui/logic/SourceSelection.h"

namespace spacer {

namespace {

constexpr int kToolbarHeight = 30;
constexpr int kRowHeight = 24;
constexpr int kMaxListHeight = 190;
const juce::Colour kPanel{0xff171a1e};
const juce::Colour kRowSelected{0xff2a3f5a};
const juce::Colour kText{0xffd9dee5};
const juce::Colour kMuted{0xff6f7782};
const juce::Colour kMeterBack{0xff0f1114};
const juce::Colour kMeter{0xff3ddc97};

}  // namespace

SourcePanel::SourcePanel(AppContext& context) : context_(context), strip_(context) {
    for (auto* b : {&addButton_, &duplicateButton_, &removeButton_}) {
        b->setWantsKeyboardFocus(false);
        addAndMakeVisible(b);
    }
    addButton_.onClick = [this] { showAddMenu(); };
    duplicateButton_.onClick = [this] { duplicateSelected(); };
    removeButton_.onClick = [this] { removeSelected(); };
    addButton_.setTooltip("Add a source");
    duplicateButton_.setTooltip("Duplicate the selected source");
    removeButton_.setTooltip("Remove the selected source (undoable)");

    list_.setModel(this);
    list_.setRowHeight(kRowHeight);
    list_.setColour(juce::ListBox::backgroundColourId, kPanel);
    list_.setWantsKeyboardFocus(false);
    addAndMakeVisible(list_);

    viewport_.setViewedComponent(&strip_, false);
    viewport_.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport_);

    context_.addListener(this);
    projectChanged(AppContext::Change::Loaded);
}

SourcePanel::~SourcePanel() {
    context_.removeListener(this);
    list_.setModel(nullptr);
}

void SourcePanel::paint(juce::Graphics& g) {
    g.fillAll(kPanel);
    g.setColour(juce::Colour(0xff2a2f36));
    g.drawVerticalLine(getWidth() - 1, 0.0f, static_cast<float>(getHeight()));
}

void SourcePanel::resized() {
    auto area = getLocalBounds().withTrimmedRight(1);
    auto bar = area.removeFromTop(kToolbarHeight).reduced(4);
    const int w = (bar.getWidth() - 8) / 3;
    addButton_.setBounds(bar.removeFromLeft(w));
    bar.removeFromLeft(4);
    duplicateButton_.setBounds(bar.removeFromLeft(w));
    bar.removeFromLeft(4);
    removeButton_.setBounds(bar);

    const int rows = static_cast<int>(context_.project().sources.size());
    const int listHeight = std::clamp(rows * kRowHeight + 2, kRowHeight * 2, kMaxListHeight);
    list_.setBounds(area.removeFromTop(listHeight));
    viewport_.setBounds(area);
    layoutStrip();
}

void SourcePanel::layoutStrip() {
    const int width = viewport_.getMaximumVisibleWidth();
    strip_.setSize(width, std::max(strip_.idealHeight(width), viewport_.getHeight()));
}

// ---- model changes ------------------------------------------------------------------

void SourcePanel::projectChanged(AppContext::Change change) {
    const bool selectionOnly = change == AppContext::Change::Selection;
    if (change == AppContext::Change::Panic || change == AppContext::Change::SpeakerTest) return;
    strip_.setSource(context_.selectedSourceId());
    if (!selectionOnly) {
        list_.updateContent();
        resized();  // the list height follows the number of sources
    }
    syncSelection();
    const bool hasSelection = context_.selectedSourceId() != 0;
    duplicateButton_.setEnabled(hasSelection);
    removeButton_.setEnabled(hasSelection);
    addButton_.setEnabled(static_cast<int>(context_.project().sources.size()) < kMaxSources);
    layoutStrip();
    list_.repaint();
}

void SourcePanel::feedbackUpdated() {
    list_.repaint();
    strip_.tick();
}

void SourcePanel::syncSelection() {
    int row = -1;
    const auto& sources = context_.project().sources;
    for (std::size_t i = 0; i < sources.size(); ++i)
        if (sources[i].id == context_.selectedSourceId()) row = static_cast<int>(i);
    const juce::ScopedValueSetter<bool> guard(syncing_, true);
    if (row >= 0)
        list_.selectRow(row, false, true);
    else
        list_.deselectAllRows();
}

// ---- list model --------------------------------------------------------------------

int SourcePanel::getNumRows() {
    return static_cast<int>(context_.project().sources.size());
}

void SourcePanel::paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) {
    const auto& sources = context_.project().sources;
    if (row < 0 || row >= static_cast<int>(sources.size())) return;
    const auto& s = sources[static_cast<std::size_t>(row)];
    if (selected) g.fillAll(kRowSelected);
    g.setColour(s.mute ? kMuted : kText);
    g.setFont(juce::FontOptions(13.0f));
    const int meterWidth = 56;
    g.drawText(juce::String::fromUTF8(s.name.c_str()), 8, 0, width - meterWidth - 20, height,
               juce::Justification::centredLeft, true);

    auto meter = juce::Rectangle<float>(static_cast<float>(width - meterWidth - 8),
                                        static_cast<float>(height) * 0.5f - 3.0f,
                                        static_cast<float>(meterWidth), 6.0f);
    g.setColour(kMeterBack);
    g.fillRoundedRectangle(meter, 2.0f);
    const float level = FeedbackState::meterFraction(context_.feedback().sourceLevelDb(s.id));
    g.setColour(s.mute ? kMuted : kMeter);
    g.fillRoundedRectangle(meter.withWidth(meter.getWidth() * level), 2.0f);
}

void SourcePanel::selectedRowsChanged(int lastRowSelected) {
    if (syncing_) return;
    const auto& sources = context_.project().sources;
    if (lastRowSelected < 0 || lastRowSelected >= static_cast<int>(sources.size())) return;
    context_.selectSource(sources[static_cast<std::size_t>(lastRowSelected)].id);
}

void SourcePanel::deleteKeyPressed(int) {
    removeSelected();
}

// ---- actions ---------------------------------------------------------------------------

void SourcePanel::showAddMenu() {
    juce::PopupMenu menu;
    int id = 1;
    for (auto kind :
         {SourceKind::VirtualInput, SourceKind::HardwareInput, SourceKind::File, SourceKind::Generator})
        menu.addItem(id++, sourceedit::sourceKindName(kind));
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&addButton_), [this](int result) {
        if (result < 1 || result > 4) return;
        static constexpr SourceKind kinds[] = {SourceKind::VirtualInput, SourceKind::HardwareInput,
                                               SourceKind::File, SourceKind::Generator};
        int newId = 0;
        if (context_.perform(sourceedit::addSource(context_.project(), kinds[result - 1], &newId)))
            context_.selectSource(newId);
    });
}

void SourcePanel::duplicateSelected() {
    int newId = 0;
    if (context_.perform(
            sourceedit::duplicateSource(context_.project(), context_.selectedSourceId(), &newId)))
        context_.selectSource(newId);
}

void SourcePanel::removeSelected() {
    const int id = context_.selectedSourceId();
    if (id == 0) return;
    // Select the neighbour first: the selection must stay valid when the
    // source disappears, and the panel then shows it right away.
    const int next = neighbourAfterRemoval(context_.project(), id);
    context_.selectSource(next);
    if (!context_.perform(sourceedit::removeSource(context_.project(), id))) context_.selectSource(id);
}

}  // namespace spacer
