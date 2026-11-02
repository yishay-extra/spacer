#include "ui/LayoutEditor.h"

#include "core/LayoutPresets.h"
#include "core/Validate.h"

namespace spacer {

namespace {

juce::String fixed(float v, int decimals) {
    return juce::String(v, decimals);
}

}  // namespace

/// A table cell that edits one speaker field on double-click.
class LayoutEditor::EditableCell final : public juce::Label {
public:
    explicit EditableCell(LayoutEditor& owner) : owner_(owner) {
        setEditable(false, true, false);
        setColour(juce::Label::textColourId, juce::Colours::white);
        onTextChange = [this] {
            if (field_) owner_.editField(speakerId_, *field_, getText());
        };
    }

    void bind(int speakerId, layoutedit::SpeakerField field, const juce::String& text) {
        speakerId_ = speakerId;
        field_ = field;
        setText(text, juce::dontSendNotification);
    }

    void mouseDown(const juce::MouseEvent& e) override {
        // Keep row selection working through the cell.
        if (auto* table = findParentComponentOfClass<juce::TableListBox>())
            table->selectRowsBasedOnModifierKeys(table->getRowNumberOfComponent(this), e.mods, false);
        juce::Label::mouseDown(e);
    }

private:
    LayoutEditor& owner_;
    int speakerId_{0};
    std::optional<layoutedit::SpeakerField> field_;
};

/// "Test" toggle: solo pink noise on the row's speaker.
class LayoutEditor::TestCell final : public juce::TextButton {
public:
    explicit TestCell(AppContext& context) : context_(context) {
        setButtonText("Test");
        setClickingTogglesState(false);
        onClick = [this] {
            const auto test = context_.speakerTestId();
            if (test && *test == speakerId_)
                context_.stopSpeakerTest();
            else
                context_.startSpeakerTest(speakerId_);
        };
    }

    void bind(int speakerId) {
        speakerId_ = speakerId;
        const auto test = context_.speakerTestId();
        setToggleState(test && *test == speakerId_, juce::dontSendNotification);
        setButtonText(getToggleState() ? "Stop" : "Test");
    }

private:
    AppContext& context_;
    int speakerId_{0};
};

LayoutEditor::LayoutEditor(AppContext& context)
    : context_(context), stage_(context, StageView::Mode::LayoutEdit) {
    int item = 1;
    for (const auto& id : layoutpresets::ids()) presetBox_.addItem(id, item++);
    presetBox_.setTextWhenNothingSelected("Preset...");
    addAndMakeVisible(presetBox_);
    applyPresetButton_.onClick = [this] { applyPreset(); };
    addButton_.onClick = [this] { addSpeaker(); };
    duplicateButton_.onClick = [this] { duplicateSpeaker(); };
    removeButton_.onClick = [this] { removeSpeaker(); };
    assignButton_.onClick = [this] { assignOutputs(); };
    for (auto* b : {&applyPresetButton_, &addButton_, &duplicateButton_, &removeButton_, &assignButton_})
        addAndMakeVisible(b);

    nameLabel_.setEditable(false, true, false);
    nameLabel_.setTooltip("Layout name (double-click to edit)");
    nameLabel_.onTextChange = [this] {
        Layout l = context_.project().layout;
        const auto text = nameLabel_.getText().trim().toStdString();
        if (text.empty() || text == l.name) return refresh();
        l.name = text;
        applyLayout(std::move(l), "Rename Layout");
    };
    addAndMakeVisible(nameLabel_);

    auto& header = table_.getHeader();
    const auto flags = juce::TableHeaderComponent::visible;
    header.addColumn("Id", kId, 36, 30, -1, flags);
    header.addColumn("Name", kName, 90, 50, -1, flags);
    header.addColumn("Az", kAzimuth, 56, 40, -1, flags);
    header.addColumn("El", kElevation, 50, 40, -1, flags);
    header.addColumn("Dist m", kDistance, 56, 40, -1, flags);
    header.addColumn("Trim dB", kTrim, 58, 40, -1, flags);
    header.addColumn("Delay ms", kDelay, 62, 40, -1, flags);
    header.addColumn("Pol", kPolarity, 40, 30, -1, flags);
    header.addColumn("Out", kOutput, 40, 30, -1, flags);
    header.addColumn("LFE", kLfe, 40, 30, -1, flags);
    header.addColumn("Comp ms", kCompDelay, 64, 40, -1, flags);
    header.addColumn("Comp dB", kCompGain, 64, 40, -1, flags);
    header.addColumn("Total ms", kEffDelay, 64, 40, -1, flags);
    header.addColumn("Total dB", kEffGain, 64, 40, -1, flags);
    header.addColumn("", kTest, 56, 50, -1, flags);
    table_.setModel(this);
    table_.setRowHeight(22);
    table_.setColour(juce::ListBox::backgroundColourId, juce::Colour(0xff1b1e22));
    addAndMakeVisible(table_);

    issuesLabel_.setJustificationType(juce::Justification::topLeft);
    issuesLabel_.setColour(juce::Label::textColourId, juce::Colour(0xffffb347));
    addAndMakeVisible(issuesLabel_);

    stage_.onSpeakerClicked = [this](int id) { selectSpeaker(id); };
    addAndMakeVisible(stage_);

    context_.addListener(this);
    refresh();
}

LayoutEditor::~LayoutEditor() {
    context_.removeListener(this);
    table_.setModel(nullptr);
}

void LayoutEditor::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff15171a));
}

void LayoutEditor::resized() {
    auto area = getLocalBounds().reduced(8);
    auto bar = area.removeFromTop(28);
    nameLabel_.setBounds(bar.removeFromLeft(180));
    bar.removeFromLeft(8);
    presetBox_.setBounds(bar.removeFromLeft(120));
    bar.removeFromLeft(4);
    applyPresetButton_.setBounds(bar.removeFromLeft(100));
    bar.removeFromLeft(16);
    for (auto* b : {&addButton_, &duplicateButton_, &removeButton_}) {
        b->setBounds(bar.removeFromLeft(84));
        bar.removeFromLeft(4);
    }
    bar.removeFromLeft(12);
    assignButton_.setBounds(bar.removeFromLeft(150));
    area.removeFromTop(8);

    auto left = area.removeFromLeft(std::min(area.getWidth() * 3 / 5, 900));
    issuesLabel_.setBounds(left.removeFromBottom(60));
    table_.setBounds(left);
    area.removeFromLeft(8);
    stage_.setBounds(area);
}

// ---- table --------------------------------------------------------------------

int LayoutEditor::getNumRows() {
    return static_cast<int>(context_.project().layout.speakers.size());
}

void LayoutEditor::paintRowBackground(juce::Graphics& g, int row, int, int, bool selected) {
    if (selected)
        g.fillAll(juce::Colour(0xff2b4a6f));
    else if (row % 2 == 1)
        g.fillAll(juce::Colour(0xff20242a));
}

void LayoutEditor::paintCell(juce::Graphics& g, int row, int columnId, int width, int height, bool) {
    const auto& speakers = context_.project().layout.speakers;
    if (row < 0 || row >= static_cast<int>(speakers.size())) return;
    const Speaker& s = speakers[static_cast<std::size_t>(row)];
    juce::String text;
    bool readOnly = true;
    const layoutedit::CompensationRow* r =
        row < static_cast<int>(rows_.size()) ? &rows_[static_cast<std::size_t>(row)] : nullptr;
    switch (columnId) {
        case kId:
            text = juce::String(s.id);
            break;
        case kCompDelay:
            text = r ? fixed(r->compensationDelayMs, 2) : "";
            break;
        case kCompGain:
            text = r ? fixed(r->compensationGainDb, 2) : "";
            break;
        case kEffDelay:
            text = r ? fixed(r->effectiveDelayMs(), 2) : "";
            break;
        case kEffGain:
            text = r ? fixed(r->effectiveGainDb(), 2) : "";
            break;
        default:
            readOnly = false;
            break;
    }
    if (!readOnly) return;
    // Values taken from a speaker chain (scene) instead of the layout are shown in italics colour.
    const bool fromChain =
        r != nullptr && r->overriddenByChain && (columnId == kEffDelay || columnId == kEffGain);
    g.setColour(fromChain ? juce::Colour(0xff9fc6ff) : juce::Colour(0xffa0a7b0));
    g.setFont(juce::FontOptions(13.0f));
    g.drawText(text, 4, 0, width - 8, height, juce::Justification::centredRight);
}

std::optional<layoutedit::SpeakerField> LayoutEditor::fieldFor(int columnId) {
    using F = layoutedit::SpeakerField;
    switch (columnId) {
        case kName:
            return F::Name;
        case kAzimuth:
            return F::Azimuth;
        case kElevation:
            return F::Elevation;
        case kDistance:
            return F::Distance;
        case kTrim:
            return F::TrimDb;
        case kDelay:
            return F::DelayMs;
        case kPolarity:
            return F::Polarity;
        case kOutput:
            return F::OutputChannel;
        case kLfe:
            return F::Lfe;
        default:
            return std::nullopt;
    }
}

juce::Component* LayoutEditor::refreshComponentForCell(int row, int columnId, bool,
                                                       juce::Component* existing) {
    const auto& speakers = context_.project().layout.speakers;
    if (row < 0 || row >= static_cast<int>(speakers.size())) {
        delete existing;
        return nullptr;
    }
    const Speaker& s = speakers[static_cast<std::size_t>(row)];
    if (columnId == kTest) {
        auto* cell = dynamic_cast<TestCell*>(existing);
        if (cell == nullptr) {
            delete existing;
            cell = new TestCell(context_);
        }
        cell->bind(s.id);
        return cell;
    }
    const auto field = fieldFor(columnId);
    if (!field) {
        delete existing;
        return nullptr;
    }
    auto* cell = dynamic_cast<EditableCell*>(existing);
    if (cell == nullptr) {
        delete existing;
        cell = new EditableCell(*this);
    }
    cell->bind(s.id, *field, juce::String(layoutedit::formatField(s, *field)));
    return cell;
}

void LayoutEditor::selectedRowsChanged(int lastRowSelected) {
    const auto& speakers = context_.project().layout.speakers;
    const int id = (lastRowSelected >= 0 && lastRowSelected < static_cast<int>(speakers.size()))
                       ? speakers[static_cast<std::size_t>(lastRowSelected)].id
                       : -1;
    stage_.setSelectedSpeaker(id);
}

int LayoutEditor::selectedSpeakerId() const {
    const int row = table_.getSelectedRow();
    const auto& speakers = context_.project().layout.speakers;
    if (row < 0 || row >= static_cast<int>(speakers.size())) return -1;
    return speakers[static_cast<std::size_t>(row)].id;
}

void LayoutEditor::selectSpeaker(int speakerId) {
    const auto& speakers = context_.project().layout.speakers;
    for (std::size_t i = 0; i < speakers.size(); ++i)
        if (speakers[i].id == speakerId) table_.selectRow(static_cast<int>(i));
}

// ---- edits --------------------------------------------------------------------

void LayoutEditor::applyLayout(Layout layout, const std::string& name) {
    context_.perform(edits::setLayout(context_.project(), std::move(layout), name));
}

void LayoutEditor::editField(int speakerId, layoutedit::SpeakerField field, const juce::String& text) {
    Layout l = context_.project().layout;
    if (!layoutedit::editSpeakerField(l, speakerId, field, text.toStdString())) {
        refresh();  // invalid or unchanged: show the model value again
        return;
    }
    const Speaker* s = l.findSpeaker(speakerId);
    const std::string name = "Edit Speaker " + (s != nullptr ? s->name : std::to_string(speakerId));
    applyLayout(std::move(l), name);
}

void LayoutEditor::applyPreset() {
    const auto id = presetBox_.getText().toStdString();
    auto l = layoutedit::preset(id);
    if (!l) return;
    applyLayout(std::move(*l), "Apply Preset " + id);
}

void LayoutEditor::addSpeaker() {
    Layout l = context_.project().layout;
    // New speakers start in front at the layout's typical distance.
    float dist = 2.0f;
    if (!l.speakers.empty()) dist = l.speakers.back().position.distance;
    const auto id = layoutedit::addSpeaker(l, {0.0f, 0.0f, dist});
    if (!id) return;
    applyLayout(std::move(l), "Add Speaker");
    selectSpeaker(*id);
}

void LayoutEditor::duplicateSpeaker() {
    const int sel = selectedSpeakerId();
    if (sel < 0) return;
    Layout l = context_.project().layout;
    const auto id = layoutedit::duplicateSpeaker(l, sel);
    if (!id) return;
    applyLayout(std::move(l), "Duplicate Speaker");
    selectSpeaker(*id);
}

void LayoutEditor::removeSpeaker() {
    const int sel = selectedSpeakerId();
    if (sel < 0) return;
    Layout l = context_.project().layout;
    if (!layoutedit::removeSpeaker(l, sel)) return;
    if (context_.speakerTestId() && *context_.speakerTestId() == sel) context_.stopSpeakerTest();
    applyLayout(std::move(l), "Remove Speaker");
}

void LayoutEditor::assignOutputs() {
    Layout l = context_.project().layout;
    layoutedit::assignOutputsSequentially(l);
    applyLayout(std::move(l), "Assign Outputs");
}

// ---- refresh ------------------------------------------------------------------

void LayoutEditor::projectChanged(AppContext::Change) {
    refresh();
}

void LayoutEditor::refresh() {
    const Project& p = context_.project();
    rows_ = layoutedit::compensationRows(p);
    nameLabel_.setText(juce::String(p.layout.name), juce::dontSendNotification);
    table_.updateContent();
    table_.repaint();

    juce::String issues;
    for (const auto& i : validate(p.layout)) {
        issues << (i.severity == Issue::Severity::Error ? "Error: " : "Warning: ") << juce::String(i.path)
               << " " << juce::String(i.message) << "\n";
    }
    issues << "Compensation at c = " << juce::String(p.devices.speedOfSound, 1)
           << " m/s (read-only; 'Total' = compensation + user delay / trim).";
    issuesLabel_.setText(issues, juce::dontSendNotification);
    removeButton_.setEnabled(p.layout.speakers.size() > 2);
    addButton_.setEnabled(p.layout.speakers.size() < static_cast<std::size_t>(kMaxSpeakers));
}

}  // namespace spacer
