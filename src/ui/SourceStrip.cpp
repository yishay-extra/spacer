#include "ui/SourceStrip.h"

#include <algorithm>

#include "ui/logic/FeedbackState.h"
#include "ui/logic/MotionFields.h"
#include "ui/logic/SourceEditing.h"

namespace spacer {

namespace {

const juce::Colour kTitle{0xffd9dee5};
const juce::Colour kMeterBack{0xff1b1e23};
const juce::Colour kMeterOk{0xff3ddc97};
const juce::Colour kMeterHot{0xffe0a030};
const juce::Colour kMeterClip{0xffe05050};

constexpr int kMargin = 8;
constexpr int kTitleHeight = 22;
constexpr int kButtonHeight = 24;
constexpr int kGap = 6;

juce::String toJuce(const std::string& s) {
    return juce::String::fromUTF8(s.c_str());
}

void styleTitle(juce::Label& l, const juce::String& text) {
    l.setText(text, juce::dontSendNotification);
    l.setColour(juce::Label::textColourId, kTitle);
    l.setFont(juce::FontOptions(13.0f, juce::Font::bold));
}

}  // namespace

void LevelMeter::paint(juce::Graphics& g) {
    auto r = getLocalBounds().toFloat();
    g.setColour(kMeterBack);
    g.fillRoundedRectangle(r, 2.0f);
    if (!levelDb) return;
    const float db = levelDb();
    const float fraction = FeedbackState::meterFraction(db);
    g.setColour(db > -1.0f ? kMeterClip : (db > -12.0f ? kMeterHot : kMeterOk));
    g.fillRoundedRectangle(r.withWidth(r.getWidth() * fraction), 2.0f);
}

SourceStrip::SourceStrip(AppContext& context)
    : context_(context),
      inserts_(
          context,
          {[this](const std::string& pluginId) {
               return sourceedit::editAddInsert(context_.project(), sourceId_, pluginId);
           },
           [this](int slot) { return sourceedit::editRemoveInsert(context_.project(), sourceId_, slot); },
           [this](int slot, bool bypassed) {
               return sourceedit::editInsertBypass(context_.project(), sourceId_, slot, bypassed);
           }}) {
    styleTitle(title_, "");
    title_.setFont(juce::FontOptions(15.0f, juce::Font::bold));
    addAndMakeVisible(title_);
    meter_.levelDb = [this] { return context_.feedback().sourceLevelDb(sourceId_); };
    addAndMakeVisible(meter_);

    styleTitle(inputTitle_, "Input");
    styleTitle(levelTitle_, "Level");
    styleTitle(spatialTitle_, "Spatializer");
    styleTitle(motionTitle_, "Motion");
    styleTitle(modulatorTitle_, "Modulators");
    for (auto* l : {&inputTitle_, &levelTitle_, &spatialTitle_, &motionTitle_, &modulatorTitle_})
        addAndMakeVisible(l);

    // Field lists: every edit is a ProjectEdit; a drag is one undo step.
    for (auto* list : {&inputList_, &levelList_, &spatialList_, &modulatorList_}) {
        list->onEdit = [this](const std::string& id, const std::string& text) {
            return apply(sourceedit::editSourceField(context_.project(), sourceId_, id, text));
        };
        list->onGestureBegin = [this] { context_.beginGesture(); };
        list->onGestureEnd = [this] { context_.endGesture(); };
        addAndMakeVisible(list);
    }
    inputList_.wantsBrowse = [](const std::string& id) { return id == "input.path"; };
    inputList_.onBrowse = [this](const std::string& id) { browseForFile(id); };
    modulatorList_.onRemoveItem = [this](int index) {
        apply(sourceedit::editRemoveModulator(context_.project(), sourceId_, index));
    };

    motionList_.onEdit = [this](const std::string& id, const std::string& text) {
        return apply(sourceedit::editMotionField(context_.project(), sourceId_, motionChannel(), id, text));
    };
    motionList_.onGestureBegin = [this] { context_.beginGesture(); };
    motionList_.onGestureEnd = [this] { context_.endGesture(); };
    motionList_.onRemoveItem = [this](int index) {
        apply(sourceedit::editMotionRemoveItem(context_.project(), sourceId_, motionChannel(), index));
    };
    addAndMakeVisible(motionList_);

    int itemId = 1;
    for (auto kind : allMotionKinds()) motionKind_.addItem(motionKindName(kind), itemId++);
    motionKind_.setWantsKeyboardFocus(false);
    motionKind_.onChange = [this] {
        if (refreshing_) return;
        const int index = motionKind_.getSelectedItemIndex();
        if (index < 0) return;
        apply(sourceedit::editMotionKind(context_.project(), sourceId_, motionChannel(),
                                         allMotionKinds()[static_cast<std::size_t>(index)]));
        refresh();  // a rejected change snaps back
    };
    addAndMakeVisible(motionKind_);

    for (auto* b : {&channelLeft_, &channelRight_}) {
        b->setClickingTogglesState(true);
        b->setRadioGroupId(7001);
        b->setWantsKeyboardFocus(false);
        addChildComponent(b);
    }
    channelLeft_.setToggleState(true, juce::dontSendNotification);
    channelLeft_.onClick = [this] {
        channel_ = 0;
        refresh();
    };
    channelRight_.onClick = [this] {
        channel_ = 1;
        refresh();
    };

    addItem_.setWantsKeyboardFocus(false);
    addItem_.onClick = [this] {
        apply(sourceedit::editMotionAddItem(context_.project(), sourceId_, motionChannel()));
    };
    addChildComponent(addItem_);
    addModulator_.setButtonText("Add modulator");
    addModulator_.setWantsKeyboardFocus(false);
    addModulator_.onClick = [this] { apply(sourceedit::editAddModulator(context_.project(), sourceId_)); };
    addAndMakeVisible(addModulator_);

    addAndMakeVisible(inserts_);
}

SourceStrip::~SourceStrip() = default;

const SourceSpec* SourceStrip::source() const {
    for (const auto& s : context_.project().sources)
        if (s.id == sourceId_) return &s;
    return nullptr;
}

int SourceStrip::motionChannel() const {
    const auto* s = source();
    return s != nullptr ? std::clamp(channel_, 0, sourceedit::motionChannelCount(*s) - 1) : 0;
}

bool SourceStrip::apply(std::optional<ProjectEdit> edit) {
    if (!edit) return false;
    return context_.perform(std::move(*edit));
}

void SourceStrip::setSource(int sourceId) {
    if (sourceId != sourceId_) channel_ = 0;
    sourceId_ = sourceId;
    refresh();
}

void SourceStrip::browseForFile(const std::string& fieldId) {
    chooser_ = std::make_unique<juce::FileChooser>("Choose an audio file", juce::File{},
                                                   "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3;*.m4a;*.aac");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, fieldId, id = sourceId_](const juce::FileChooser& fc) {
                              const auto file = fc.getResult();
                              if (file == juce::File{}) return;
                              apply(sourceedit::editSourceField(context_.project(), id, fieldId,
                                                                file.getFullPathName().toStdString()));
                          });
}

void SourceStrip::refresh() {
    const juce::ScopedValueSetter<bool> guard(refreshing_, true);
    const SourceSpec* s = source();
    const bool has = s != nullptr;
    for (auto* c : std::initializer_list<juce::Component*>{
             &title_, &meter_, &inputTitle_, &levelTitle_, &spatialTitle_, &motionTitle_, &modulatorTitle_,
             &inputList_, &levelList_, &spatialList_, &motionList_, &modulatorList_, &motionKind_,
             &addModulator_, &inserts_})
        c->setVisible(has);
    if (!has) {
        channelLeft_.setVisible(false);
        channelRight_.setVisible(false);
        addItem_.setVisible(false);
        resized();
        repaint();
        return;
    }
    const auto& layout = context_.project().layout;
    title_.setText(toJuce(s->name + "  -  " + sourceedit::sourceKindName(s->kind())),
                   juce::dontSendNotification);
    inputList_.setFields(sourceedit::sourceFields(*s, layout, sourceedit::SourceSection::Input));
    levelList_.setFields(sourceedit::sourceFields(*s, layout, sourceedit::SourceSection::Level));
    spatialList_.setFields(sourceedit::sourceFields(*s, layout, sourceedit::SourceSection::Spatial));
    modulatorList_.setFields(sourceedit::sourceFields(*s, layout, sourceedit::SourceSection::Modulators));
    addModulator_.setEnabled(static_cast<int>(s->modulators.size()) < kMaxModulators);

    const int channels = sourceedit::motionChannelCount(*s);
    channel_ = std::clamp(channel_, 0, channels - 1);
    const MotionSpec& motion = sourceedit::motionOf(*s, channel_);
    motionKind_.setSelectedItemIndex(static_cast<int>(motionKindOf(motion)), juce::dontSendNotification);
    motionList_.setFields(motionFields(motion));
    channelLeft_.setVisible(channels == 2);
    channelRight_.setVisible(channels == 2);
    channelLeft_.setToggleState(channel_ == 0, juce::dontSendNotification);
    channelRight_.setToggleState(channel_ == 1, juce::dontSendNotification);
    const auto itemName = motionItemName(motion);
    addItem_.setVisible(!itemName.empty());
    addItem_.setButtonText("Add " + toJuce(itemName));

    inserts_.setOwner(InsertOwner::Source, sourceId_);

    resized();
    repaint();
}

void SourceStrip::tick() {
    if (source() == nullptr) return;
    meter_.repaint();
    inserts_.tick();  // plug-ins that finished loading
}

int SourceStrip::idealHeight(int width) {
    layoutContent(width, false);
    return contentHeight_;
}

void SourceStrip::paint(juce::Graphics& g) {
    if (source() != nullptr) return;
    g.setColour(juce::Colour(0xff6f7782));
    g.setFont(juce::FontOptions(13.0f));
    g.drawText("No source selected", getLocalBounds().reduced(kMargin), juce::Justification::centred);
}

void SourceStrip::resized() {
    layoutContent(getWidth(), true);
}

void SourceStrip::layoutContent(int width, bool place) {
    if (source() == nullptr) {
        contentHeight_ = 80;
        return;
    }
    const int inner = std::max(0, width - 2 * kMargin);
    int y = kMargin;
    const auto put = [&](juce::Component& c, int h, int x = kMargin, int w = -1) {
        if (place) c.setBounds(x, y, w < 0 ? inner : w, h);
        y += h;
    };
    const auto gap = [&] { y += kGap; };

    // Title + meter.
    if (place) {
        title_.setBounds(kMargin, y, inner, kTitleHeight);
        meter_.setBounds(kMargin, y + kTitleHeight + 2, inner, 6);
    }
    y += kTitleHeight + 10;
    gap();

    const auto list = [&](juce::Label& title, FieldList& fields) {
        put(title, kTitleHeight);
        if (place) fields.setBounds(kMargin, y, inner, fields.idealHeight());
        y += fields.idealHeight();
        gap();
    };
    list(inputTitle_, inputList_);
    list(levelTitle_, levelList_);
    list(spatialTitle_, spatialList_);

    // Motion: kind (+ left / right for linked points), parameters, list button.
    put(motionTitle_, kTitleHeight);
    const bool two = channelLeft_.isVisible();
    if (place) {
        const int kindWidth = two ? inner - 120 : inner;
        motionKind_.setBounds(kMargin, y, kindWidth, kButtonHeight);
        if (two) {
            channelLeft_.setBounds(kMargin + kindWidth + 4, y, 54, kButtonHeight);
            channelRight_.setBounds(kMargin + kindWidth + 62, y, 54, kButtonHeight);
        }
    }
    y += kButtonHeight + 2;
    if (place) motionList_.setBounds(kMargin, y, inner, motionList_.idealHeight());
    y += motionList_.idealHeight();
    if (addItem_.isVisible()) put(addItem_, kButtonHeight, kMargin, std::min(inner, 140));
    gap();

    list(modulatorTitle_, modulatorList_);
    put(addModulator_, kButtonHeight, kMargin, std::min(inner, 140));
    gap();

    put(inserts_, inserts_.idealHeight());
    contentHeight_ = y + kMargin;
}

}  // namespace spacer
