#include "ui/FieldList.h"

#include "ui/logic/ValueFormat.h"

namespace spacer {

namespace {

const juce::Colour kLabel{0xffa0a7b0};
const juce::Colour kHeader{0xff6f7782};
const juce::Colour kAccent{0xff4fa3ff};

juce::String toJuce(const std::string& s) {
    return juce::String::fromUTF8(s.c_str());
}

std::string toStd(const juce::String& s) {
    return s.toStdString();
}

}  // namespace

/// One labelled control. Notifications are suppressed while the row is being
/// updated from the model, so a refresh never produces an edit.
class FieldList::Row final : public juce::Component {
public:
    Row(FieldList& owner, const MotionField& field) : owner_(owner), type_(field.type), id_(field.id) {
        label_.setText(toJuce(field.label), juce::dontSendNotification);
        label_.setColour(juce::Label::textColourId, kLabel);
        label_.setFont(juce::FontOptions(12.0f));
        addAndMakeVisible(label_);
        switch (type_) {
            case FieldType::Number:
                setupSlider(field);
                break;
            case FieldType::Period:
            case FieldType::Text:
                setupText();
                break;
            case FieldType::Choice:
                setupChoice(field);
                break;
            case FieldType::Toggle:
                setupToggle();
                break;
        }
        removeButton_.setButtonText("x");
        removeButton_.setWantsKeyboardFocus(false);
        removeButton_.onClick = [this] {
            if (owner_.onRemoveItem && item_ >= 0) owner_.onRemoveItem(item_);
        };
        addChildComponent(removeButton_);
        browseButton_.setButtonText("...");
        browseButton_.setWantsKeyboardFocus(false);
        browseButton_.onClick = [this] {
            if (owner_.onBrowse) owner_.onBrowse(id_);
        };
        addChildComponent(browseButton_);
    }

    const std::string& id() const { return id_; }

    void update(const MotionField& field) {
        const juce::ScopedValueSetter<bool> guard(updating_, true);
        label_.setText(toJuce(field.label), juce::dontSendNotification);
        switch (type_) {
            case FieldType::Number:
                unit_ = field.unit;
                if (!slider_.isMouseButtonDown())  // keep a drag steady slider_.setValue(field.value,
                                                   // juce::dontSendNotification);
                    break;
            case FieldType::Period:
            case FieldType::Text:
                if (!text_.isBeingEdited()) text_.setText(toJuce(field.text), juce::dontSendNotification);
                break;
            case FieldType::Choice:
                combo_.setSelectedItemIndex(field.choice, juce::dontSendNotification);
                break;
            case FieldType::Toggle:
                toggle_.setToggleState(field.on, juce::dontSendNotification);
                break;
        }
    }

    /// Show a remove button for list item `item` (-1: none).
    void setItem(int item, bool removable, bool browsable) {
        item_ = item;
        removeButton_.setVisible(removable);
        browseButton_.setVisible(browsable);
        resized();
    }

    void resized() override {
        auto area = getLocalBounds().reduced(2, 1);
        if (removeButton_.isVisible()) removeButton_.setBounds(area.removeFromRight(20));
        if (browseButton_.isVisible()) browseButton_.setBounds(area.removeFromRight(26));
        label_.setBounds(area.removeFromLeft(juce::jmin(110, area.getWidth() / 2)));
        area.removeFromLeft(4);
        switch (type_) {
            case FieldType::Number:
                slider_.setBounds(area);
                break;
            case FieldType::Period:
            case FieldType::Text:
                text_.setBounds(area);
                break;
            case FieldType::Choice:
                combo_.setBounds(area);
                break;
            case FieldType::Toggle:
                toggle_.setBounds(area);
                break;
        }
    }

private:
    void edited(const std::string& text) {
        if (updating_ || !owner_.onEdit) return;
        // Rejected or unchanged: show the model's value again.
        if (!owner_.onEdit(id_, text)) owner_.updateRows();
    }

    void setupSlider(const MotionField& field) {
        unit_ = field.unit;
        slider_.setSliderStyle(juce::Slider::LinearBar);
        slider_.setWantsKeyboardFocus(false);
        slider_.setColour(juce::Slider::trackColourId, kAccent.withAlpha(0.35f));
        slider_.setColour(juce::Slider::backgroundColourId, juce::Colour(0xff23272e));
        slider_.setColour(juce::Slider::textBoxTextColourId, juce::Colours::white);
        slider_.setTextBoxIsEditable(true);
        const double interval = field.unit == fmt::Unit::Count ? 1.0 : 0.0;
        slider_.setRange(static_cast<double>(field.min), static_cast<double>(field.max), interval);
        if (field.unit == fmt::Unit::Db && field.min < -40.0f && field.max > 0.0f)
            slider_.setSkewFactorFromMidPoint(-18.0);
        slider_.textFromValueFunction = [this](double v) {
            return toJuce(fmt::format(static_cast<float>(v), unit_));
        };
        slider_.valueFromTextFunction = [this](const juce::String& t) {
            const auto v = fmt::parse(toStd(t), unit_);
            return v ? static_cast<double>(*v) : slider_.getValue();
        };
        slider_.setValue(static_cast<double>(field.value), juce::dontSendNotification);
        slider_.onDragStart = [this] {
            if (owner_.onGestureBegin) owner_.onGestureBegin();
        };
        slider_.onDragEnd = [this] {
            if (owner_.onGestureEnd) owner_.onGestureEnd();
        };
        slider_.onValueChange = [this] {
            edited(fmt::formatBare(static_cast<float>(slider_.getValue()), unit_));
        };
        addAndMakeVisible(slider_);
    }

    void setupText() {
        text_.setEditable(true);
        text_.setColour(juce::Label::backgroundColourId, juce::Colour(0xff23272e));
        text_.setColour(juce::Label::textColourId, juce::Colours::white);
        text_.setColour(juce::Label::outlineColourId, juce::Colour(0xff343a43));
        text_.setFont(juce::FontOptions(12.0f));
        text_.setMinimumHorizontalScale(0.7f);
        text_.onTextChange = [this] { edited(toStd(text_.getText())); };
        addAndMakeVisible(text_);
    }

    void setupChoice(const MotionField& field) {
        combo_.setWantsKeyboardFocus(false);
        int index = 1;
        for (const auto& c : field.choices) combo_.addItem(toJuce(c), index++);
        combo_.setSelectedItemIndex(field.choice, juce::dontSendNotification);
        combo_.onChange = [this] {
            const int i = combo_.getSelectedItemIndex();
            if (i >= 0) edited(std::to_string(i));
        };
        addAndMakeVisible(combo_);
    }

    void setupToggle() {
        toggle_.setWantsKeyboardFocus(false);
        toggle_.onClick = [this] { edited(toggle_.getToggleState() ? "1" : "0"); };
        addAndMakeVisible(toggle_);
    }

    FieldList& owner_;
    FieldType type_;
    std::string id_;
    fmt::Unit unit_{fmt::Unit::None};
    int item_{-1};
    bool updating_{false};

    juce::Label label_;
    juce::Slider slider_;
    juce::Label text_;
    juce::ComboBox combo_;
    juce::ToggleButton toggle_;
    juce::TextButton removeButton_;
    juce::TextButton browseButton_;
};

FieldList::FieldList() = default;
FieldList::~FieldList() = default;

void FieldList::setFields(std::vector<MotionField> fields) {
    std::vector<std::string> ids;
    ids.reserve(fields.size());
    for (const auto& f : fields) {
        std::string key = f.id + "|" + std::to_string(static_cast<int>(f.type)) + "|" + f.group + "|" +
                          std::to_string(static_cast<int>(f.unit)) + "|" + std::to_string(f.item);
        for (const auto& c : f.choices) key += "|" + c;  // a different choice list needs a new combo box
        ids.push_back(std::move(key));
    }
    const bool sameShape = ids == ids_;
    fields_ = std::move(fields);
    if (sameShape) {
        updateRows();
    } else {
        ids_ = std::move(ids);
        rebuild();
    }
}

void FieldList::rebuild() {
    entries_.clear();
    headers_.clear();
    removeAllChildren();
    std::string lastGroup;
    for (const auto& f : fields_) {
        Entry e;
        if (f.group != lastGroup && !f.group.empty()) e.headerText = f.group;
        lastGroup = f.group;
        e.row = std::make_unique<Row>(*this, f);
        const bool firstOfItem = f.item >= 0 && !e.headerText.empty();
        e.row->setItem(f.item, firstOfItem && onRemoveItem != nullptr,
                       f.type == FieldType::Text && wantsBrowse != nullptr && wantsBrowse(f.id));
        addAndMakeVisible(*e.row);
        if (!e.headerText.empty()) {
            auto header = std::make_unique<juce::Label>();
            header->setText(toJuce(e.headerText), juce::dontSendNotification);
            header->setColour(juce::Label::textColourId, kHeader);
            header->setFont(juce::FontOptions(11.0f, juce::Font::bold));
            addAndMakeVisible(*header);
            headers_.push_back(std::move(header));
        }
        entries_.push_back(std::move(e));
    }
    updateRows();
    resized();
}

void FieldList::updateRows() {
    for (std::size_t i = 0; i < entries_.size() && i < fields_.size(); ++i)
        entries_[i].row->update(fields_[i]);
}

int FieldList::idealHeight() const {
    int h = 0;
    for (const auto& e : entries_) {
        if (!e.headerText.empty()) h += kHeaderHeight;
        h += kRowHeight;
    }
    return h;
}

void FieldList::resized() {
    auto area = getLocalBounds();
    std::size_t header = 0;
    for (auto& e : entries_) {
        if (!e.headerText.empty() && header < headers_.size())
            headers_[header++]->setBounds(area.removeFromTop(kHeaderHeight));
        e.row->setBounds(area.removeFromTop(kRowHeight));
    }
}

}  // namespace spacer
