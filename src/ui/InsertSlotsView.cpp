#include "ui/InsertSlotsView.h"

#include <algorithm>

#include "ui/PluginUi.h"
#include "ui/logic/InsertEditing.h"

namespace spacer {

namespace {
constexpr int kRowHeight = 26;
constexpr int kTitleHeight = 22;
constexpr int kButtonHeight = 24;
constexpr int kGap = 4;
const juce::Colour kTitle{0xffd9dee5};
const juce::Colour kDim{0xffa0a7b0};
const juce::Colour kMissing{0xffe08060};
}  // namespace

/// One slot: name, bypass, Edit, remove.
class InsertSlotsView::Row final : public juce::Component {
public:
    Row(InsertSlotsView& owner, int slot) : owner_(owner), slot_(slot) {
        name_.setFont(juce::FontOptions(13.0f));
        name_.setMinimumHorizontalScale(0.8f);
        addAndMakeVisible(name_);
        bypass_.setButtonText("Bypass");
        bypass_.onClick = [this] {
            owner_.apply(owner_.edits_.bypass ? owner_.edits_.bypass(slot_, bypass_.getToggleState())
                                              : std::nullopt);
        };
        addAndMakeVisible(bypass_);
        edit_.setButtonText("Edit");
        edit_.setWantsKeyboardFocus(false);
        edit_.onClick = [this] {
            const auto message = owner_.context_.pluginUi().openEditor(owner_.owner_, owner_.ownerId_, slot_);
            if (!message.empty())
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                                       "Plug-in editor",
                                                       juce::String::fromUTF8(message.c_str()));
        };
        addAndMakeVisible(edit_);
        remove_.setButtonText("X");
        remove_.setTooltip("Remove this insert");
        remove_.setWantsKeyboardFocus(false);
        remove_.onClick = [this] {
            owner_.apply(owner_.edits_.remove ? owner_.edits_.remove(slot_) : std::nullopt);
        };
        addAndMakeVisible(remove_);
    }

    void bind(const juce::String& label, bool bypassed, bool loaded, bool empty) {
        name_.setText(label, juce::dontSendNotification);
        name_.setColour(juce::Label::textColourId, loaded || empty ? kTitle : kMissing);
        name_.setTooltip(loaded || empty ? "" : "Plug-in not found or failed to load: the slot is bypassed");
        bypass_.setToggleState(bypassed, juce::dontSendNotification);
        edit_.setEnabled(loaded);
    }

    void resized() override {
        auto r = getLocalBounds();
        remove_.setBounds(r.removeFromRight(26).reduced(0, 1));
        r.removeFromRight(kGap);
        edit_.setBounds(r.removeFromRight(46).reduced(0, 1));
        r.removeFromRight(kGap);
        bypass_.setBounds(r.removeFromRight(70));
        name_.setBounds(r);
    }

private:
    InsertSlotsView& owner_;
    int slot_;
    juce::Label name_;
    juce::ToggleButton bypass_;
    juce::TextButton edit_, remove_;
};

InsertSlotsView::InsertSlotsView(AppContext& context, Edits edits)
    : context_(context), edits_(std::move(edits)) {
    title_.setText("Inserts", juce::dontSendNotification);
    title_.setColour(juce::Label::textColourId, kTitle);
    title_.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    addAndMakeVisible(title_);
    hint_.setText("No inserts.", juce::dontSendNotification);
    hint_.setColour(juce::Label::textColourId, kDim);
    hint_.setFont(juce::FontOptions(12.0f));
    addAndMakeVisible(hint_);
    addButton_.setWantsKeyboardFocus(false);
    addButton_.onClick = [this] { showAddMenu(); };
    addAndMakeVisible(addButton_);
    pluginsButton_.setWantsKeyboardFocus(false);
    pluginsButton_.setTooltip("Scan for plug-ins, see the blacklist");
    pluginsButton_.onClick = [this] { context_.pluginUi().showPluginManager(); };
    addAndMakeVisible(pluginsButton_);
}

InsertSlotsView::~InsertSlotsView() = default;

void InsertSlotsView::setOwner(InsertOwner owner, int ownerId) {
    if (owner != owner_ || ownerId != ownerId_) signature_.clear();
    owner_ = owner;
    ownerId_ = ownerId;
    refresh();
}

bool InsertSlotsView::apply(std::optional<ProjectEdit> edit) {
    return edit && context_.perform(std::move(*edit));
}

void InsertSlotsView::refresh() {
    rebuild();
}

void InsertSlotsView::tick() {
    rebuild();  // only touches the rows if their content changed
}

void InsertSlotsView::rebuild() {
    const auto* slots = ownerId_ >= 0 ? insertedit::slotsOf(context_.project(), owner_, ownerId_) : nullptr;
    const int count = slots != nullptr ? static_cast<int>(slots->size()) : 0;

    struct Info {
        juce::String label;
        bool bypassed, loaded, empty;
    };
    std::vector<Info> infos;
    std::string signature = std::to_string(ownerId_) + ":";
    for (int i = 0; i < count; ++i) {
        const PluginSlot& slot = (*slots)[static_cast<std::size_t>(i)];
        Info info;
        info.empty = slot.pluginId.empty();
        info.bypassed = slot.bypassed;
        info.loaded = info.empty || context_.plugins().instanceFor(owner_, ownerId_, i) != nullptr;
        juce::String name = juce::String::fromUTF8(insertedit::fallbackName(slot.pluginId).c_str());
        if (const auto d = context_.plugins().cache().find(slot.pluginId)) name = d->name;
        info.label = juce::String(i + 1) + "  " + name + (info.loaded ? "" : "  (missing)");
        signature += info.label.toStdString() + (info.bypassed ? "!" : "") + (info.loaded ? "+" : "-") + "|";
        infos.push_back(std::move(info));
    }
    if (signature == signature_) return;
    signature_ = signature;

    while (static_cast<int>(rows_.size()) > count) rows_.pop_back();
    while (static_cast<int>(rows_.size()) < count) {
        rows_.push_back(std::make_unique<Row>(*this, static_cast<int>(rows_.size())));
        addAndMakeVisible(*rows_.back());
    }
    for (int i = 0; i < count; ++i) {
        const auto& info = infos[static_cast<std::size_t>(i)];
        rows_[static_cast<std::size_t>(i)]->bind(info.label, info.bypassed, info.loaded, info.empty);
    }
    hint_.setVisible(count == 0);
    addButton_.setEnabled(ownerId_ >= 0 && count < kMaxInserts);
    resized();
}

int InsertSlotsView::idealHeight() const {
    int h = kTitleHeight + kGap;
    h += rows_.empty() ? 20 : static_cast<int>(rows_.size()) * kRowHeight;
    return h + kGap + kButtonHeight;
}

void InsertSlotsView::resized() {
    auto r = getLocalBounds();
    title_.setBounds(r.removeFromTop(kTitleHeight));
    r.removeFromTop(kGap);
    if (rows_.empty()) {
        hint_.setBounds(r.removeFromTop(20));
    } else {
        for (auto& row : rows_) row->setBounds(r.removeFromTop(kRowHeight));
    }
    r.removeFromTop(kGap);
    auto buttons = r.removeFromTop(kButtonHeight);
    addButton_.setBounds(buttons.removeFromLeft(std::min(buttons.getWidth() / 2, 120)));
    buttons.removeFromLeft(kGap);
    pluginsButton_.setBounds(buttons.removeFromLeft(std::min(buttons.getWidth(), 100)));
}

void InsertSlotsView::showAddMenu() {
    juce::PopupMenu menu;
    std::vector<juce::String> ids;
    for (const auto& d : context_.plugins().cache().known()) {
        if (d.isInstrument) continue;  // inserts are effects (SPEC §13)
        ids.push_back(d.fileOrIdentifier);
        menu.addItem(static_cast<int>(ids.size()), d.name + "  (" + d.manufacturerName + ")");
    }
    if (ids.empty()) menu.addItem(-1, "No scanned plug-ins yet", false, false);
    menu.addSeparator();
    constexpr int kManage = 100000;
    menu.addItem(kManage, "Scan for plug-ins...");
    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(&addButton_),
        [safe = juce::Component::SafePointer<InsertSlotsView>(this), ids](int result) {
            if (safe == nullptr || result <= 0) return;
            if (result == kManage) {
                safe->context_.pluginUi().showPluginManager();
                return;
            }
            if (result > static_cast<int>(ids.size())) return;
            safe->apply(safe->edits_.add
                            ? safe->edits_.add(ids[static_cast<std::size_t>(result - 1)].toStdString())
                            : std::nullopt);
        });
}

}  // namespace spacer
