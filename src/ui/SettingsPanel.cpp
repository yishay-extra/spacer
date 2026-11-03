#include "ui/SettingsPanel.h"

#include <algorithm>
#include <cmath>
#include <iterator>

#include "ui/logic/AppSettings.h"
#include "ui/logic/StatusBarModel.h"

namespace spacer {

namespace {

constexpr int kLabelWidth = 190;
constexpr int kRowGap = 6;
constexpr int kMargin = 16;

juce::Component::SafePointer<juce::DialogWindow> gDialog;

void styleEditor(juce::TextEditor& e, int maxChars) {
    e.setMultiLine(false);
    e.setReturnKeyStartsNewLine(false);
    e.setInputRestrictions(maxChars);
    e.setSelectAllWhenFocused(true);
}

void styleHeader(juce::Label& l) {
    l.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    l.setColour(juce::Label::textColourId, juce::Colour(0xff7fb2ff));
}

void styleNote(juce::Label& l) {
    l.setFont(juce::FontOptions(12.0f));
    l.setColour(juce::Label::textColourId, juce::Colour(0xffa0a7b0));
    l.setMinimumHorizontalScale(0.75f);
}

/// Name field and one toggle per candidate device, shown inside an AlertWindow.
class AggregateChooser final : public juce::Component {
public:
    AggregateChooser(const std::vector<settings::AudioDeviceInfo>& candidates) : candidates_(candidates) {
        nameLabel_.setText("Name", juce::dontSendNotification);
        addAndMakeVisible(nameLabel_);
        styleEditor(name_, 60);
        name_.setText("Spacer Aggregate", false);
        addAndMakeVisible(name_);
        for (const auto& d : candidates_) {
            auto t = std::make_unique<juce::ToggleButton>(juce::String(d.name) + "  (" +
                                                          juce::String(d.numOutputs) + " out)");
            addAndMakeVisible(*t);
            toggles_.push_back(std::move(t));
        }
        setSize(380, 34 + static_cast<int>(candidates_.size()) * 26 + 8);
    }

    void resized() override {
        auto a = getLocalBounds();
        auto top = a.removeFromTop(28);
        nameLabel_.setBounds(top.removeFromLeft(60));
        name_.setBounds(top);
        a.removeFromTop(6);
        for (auto& t : toggles_) t->setBounds(a.removeFromTop(26));
    }

    std::string name() const { return name_.getText().toStdString(); }
    /// UIDs of the ticked devices in list (channel) order. The first is the clock.
    std::vector<std::string> chosen() const {
        std::vector<std::string> out;
        for (std::size_t i = 0; i < toggles_.size(); ++i)
            if (toggles_[i]->getToggleState()) out.push_back(candidates_[i].uid);
        return out;
    }

private:
    std::vector<settings::AudioDeviceInfo> candidates_;
    juce::Label nameLabel_;
    juce::TextEditor name_;
    std::vector<std::unique_ptr<juce::ToggleButton>> toggles_;
};

int comboIndex(const juce::ComboBox& box) {
    return box.getSelectedId() - 1;
}

std::string text(const juce::TextEditor& e) {
    return e.getText().toStdString();
}

}  // namespace

SettingsPanel::SettingsPanel(AppContext& context) : context_(context) {
    for (auto* h : {&headerAudio_, &headerVdev_, &headerOsc_, &headerMidi_, &headerClock_, &headerInstall_}) {
        styleHeader(*h);
        addAndMakeVisible(*h);
    }
    for (auto* l : {&deviceLabel_, &rateLabel_, &bufferLabel_, &speedLabel_, &driverLabel_,
                    &vdevLatencyLabel_, &oscInLabel_, &midiInputsLabel_, &pcLabel_, &clockLabel_, &bpmLabel_,
                    &bpbLabel_, &trimLabel_, &autosaveLabel_})
        addAndMakeVisible(*l);
    for (auto* n : {&audioStatus_, &driverStatus_, &oscStatus_, &clockStatus_, &clockHint_}) {
        styleNote(*n);
        addAndMakeVisible(*n);
    }
    errorLabel_.setColour(juce::Label::textColourId, juce::Colour(0xffef6f6f));
    errorLabel_.setFont(juce::FontOptions(12.0f));
    addAndMakeVisible(errorLabel_);

    for (auto* e : {&speedEdit_, &oscInEdit_, &oscHostEdit_, &oscOutPortEdit_, &bpmEdit_, &bpbEdit_,
                    &trimEdit_, &autosaveEdit_})
        addAndMakeVisible(*e);
    styleEditor(speedEdit_, 6);
    styleEditor(oscInEdit_, 5);
    styleEditor(oscHostEdit_, 253);
    styleEditor(oscOutPortEdit_, 5);
    styleEditor(bpmEdit_, 7);
    styleEditor(bpbEdit_, 2);
    styleEditor(trimEdit_, 7);
    trimEdit_.setTooltip(
        "Added to the automatic compensation (device output latency + engine latency) "
        "so an external clock's beat is the one heard");
    styleEditor(autosaveEdit_, 5);
    for (auto* b : {&deviceBox_, &rateBox_, &bufferBox_, &vdevLatencyBox_, &pcBox_, &clockBox_})
        addAndMakeVisible(*b);
    for (auto* t : {&oscOutToggle_, &quantiseToggle_, &installationToggle_}) addAndMakeVisible(*t);
    addAndMakeVisible(aggregateButton_);
    addAndMakeVisible(installButton_);

    // Static choices.
    for (std::size_t i = 0; i < std::size(appsettings::kVirtualLatencyChoices); ++i)
        vdevLatencyBox_.addItem("-", static_cast<int>(i) + 1);  // text set in refresh() (needs the rate)
    pcBox_.addItem("Off", 1);
    for (int ch = 1; ch <= 16; ++ch) pcBox_.addItem("Channel " + juce::String(ch), ch + 1);
    clockOptions_ = settings::clockOptions(ClockInputs::linkAvailable());
    for (std::size_t i = 0; i < clockOptions_.size(); ++i)
        clockBox_.addItem(juce::String(clockOptions_[i].label), static_cast<int>(i) + 1);
    midiNames_ = context_.midiInputNames();
    for (const auto& name : midiNames_) {
        auto t = std::make_unique<juce::ToggleButton>(juce::String(name));
        t->onClick = [this] { commitApp(); };
        addAndMakeVisible(*t);
        midiToggles_.push_back(std::move(t));
    }

    // Commit handlers.
    deviceBox_.onChange = [this] {
        if (!refreshing_) commit("Change Audio Device");
    };
    rateBox_.onChange = [this] {
        if (!refreshing_) commit("Change Sample Rate");
    };
    bufferBox_.onChange = [this] {
        if (!refreshing_) commit("Change Buffer Size");
    };
    pcBox_.onChange = [this] {
        if (!refreshing_) commit("Change Program-Change Channel");
    };
    clockBox_.onChange = [this] {
        if (!refreshing_) commit("Change Clock Source");
    };
    oscOutToggle_.onClick = [this] {
        if (!refreshing_) commit("Change OSC Output");
    };
    quantiseToggle_.onClick = [this] {
        if (!refreshing_) commit("Change Clock");
    };
    const std::pair<juce::TextEditor*, const char*> editors[] = {{&speedEdit_, "Change Speed of Sound"},
                                                                 {&oscInEdit_, "Change OSC Port"},
                                                                 {&oscHostEdit_, "Change OSC Output"},
                                                                 {&oscOutPortEdit_, "Change OSC Output"},
                                                                 {&bpmEdit_, "Change Tempo"},
                                                                 {&bpbEdit_, "Change Beats per Bar"},
                                                                 {&trimEdit_, "Change Clock Latency Trim"}};
    for (const auto& [editor, name] : editors) {
        editor->onReturnKey = [this, name] {
            if (!refreshing_) commit(name);
        };
        editor->onFocusLost = [this, name] {
            if (!refreshing_) commit(name);
        };
    }
    vdevLatencyBox_.onChange = [this] {
        if (!refreshing_) commitApp();
    };
    installationToggle_.onClick = [this] {
        if (!refreshing_) commitInstallation();
    };
    autosaveEdit_.onReturnKey = [this] {
        if (!refreshing_) commitInstallation();
    };
    autosaveEdit_.onFocusLost = [this] {
        if (!refreshing_) commitInstallation();
    };
    aggregateButton_.onClick = [this] { createAggregate(); };
    installButton_.onClick = [this] { installDriver(); };
    aggregateButton_.setTooltip(
        "Combine several output interfaces into one device (one clock, drift-compensated)");

    // Rows (top to bottom); resized() places them.
    rows_ = {
        {&headerAudio_, {}, 22, true},
        {&deviceLabel_, {&deviceBox_}},
        {&rateLabel_, {&rateBox_}},
        {&bufferLabel_, {&bufferBox_}},
        {&speedLabel_, {&speedEdit_}},
        {nullptr, {&aggregateButton_}},
        {nullptr, {&audioStatus_}, 18},
        {&headerVdev_, {}, 22, true},
        {&driverLabel_, {&driverStatus_, &installButton_}},
        {&vdevLatencyLabel_, {&vdevLatencyBox_}},
        {&headerOsc_, {}, 22, true},
        {&oscInLabel_, {&oscInEdit_}},
        {nullptr, {&oscOutToggle_, &oscHostEdit_, &oscOutPortEdit_}},
        {nullptr, {&oscStatus_}, 18},
        {&headerMidi_, {}, 22, true},
    };
    for (std::size_t i = 0; i < midiToggles_.size(); ++i)
        rows_.push_back({i == 0 ? &midiInputsLabel_ : nullptr, {midiToggles_[i].get()}, 22});
    if (midiToggles_.empty()) {
        midiInputsLabel_.setText("MIDI inputs: none found", juce::dontSendNotification);
        rows_.push_back({&midiInputsLabel_, {}});
    }
    rows_.insert(rows_.end(), {
                                  {&pcLabel_, {&pcBox_}},
                                  {&headerClock_, {}, 22, true},
                                  {&clockLabel_, {&clockBox_, &clockStatus_}},
                                  {&bpmLabel_, {&bpmEdit_}},
                                  {&bpbLabel_, {&bpbEdit_}},
                                  {&trimLabel_, {&trimEdit_}},
                                  {nullptr, {&quantiseToggle_}},
                                  {nullptr, {&clockHint_}, 18},
                                  {&headerInstall_, {}, 22, true},
                                  {nullptr, {&installationToggle_}},
                                  {&autosaveLabel_, {&autosaveEdit_}},
                                  {nullptr, {&errorLabel_}, 18},
                              });
    int h = kMargin;
    for (const auto& r : rows_) h += r.height + kRowGap;
    setSize(kWidth, h + kMargin);

    context_.addListener(this);
    refresh();
}

SettingsPanel::~SettingsPanel() {
    context_.removeListener(this);
}

void SettingsPanel::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff1e2126));
}

void SettingsPanel::resized() {
    int y = kMargin;
    const int x0 = kMargin;
    const int width = getWidth() - 2 * kMargin;
    for (const auto& r : rows_) {
        juce::Rectangle<int> row(x0, y, width, r.height);
        if (r.header) {
            r.label->setBounds(row);
        } else {
            // Rows without a label are indented to the control column.
            if (r.label != nullptr)
                r.label->setBounds(row.removeFromLeft(kLabelWidth));
            else if (r.controls.size() == 1)
                row.removeFromLeft(kLabelWidth);
            const int n = static_cast<int>(r.controls.size());
            if (n == 1) {
                auto* c = r.controls[0];
                const bool narrow = dynamic_cast<juce::TextEditor*>(c) != nullptr;
                const bool button = dynamic_cast<juce::TextButton*>(c) != nullptr;
                c->setBounds(narrow ? row.removeFromLeft(110) : button ? row.removeFromLeft(220) : row);
            } else if (n >= 2) {
                // OSC output row: toggle | host | port ; driver row: status | button ; clock row: box |
                // status.
                if (r.controls[0] == &oscOutToggle_) {
                    // Toggle sits in the label column.
                    oscOutToggle_.setBounds(juce::Rectangle<int>(x0, y, kLabelWidth, r.height));
                    oscHostEdit_.setBounds(row.removeFromLeft(220));
                    row.removeFromLeft(8);
                    oscOutPortEdit_.setBounds(row.removeFromLeft(80));
                } else if (r.controls[0] == &driverStatus_) {
                    installButton_.setBounds(row.removeFromRight(190));
                    driverStatus_.setBounds(row);
                } else {
                    r.controls[0]->setBounds(row.removeFromLeft(180));
                    row.removeFromLeft(10);
                    r.controls[1]->setBounds(row);
                }
            }
        }
        y += r.height + kRowGap;
    }
}

// ---- model -> controls ----------------------------------------------------------

void SettingsPanel::refreshChoices() {
    const auto& project = context_.project();
    auto& dm = context_.engine().deviceManager();
    auto* device = dm.getCurrentAudioDevice();

    // Output devices of the current device type; "" = the system default.
    deviceNames_ = {""};
    deviceBox_.clear(juce::dontSendNotification);
    deviceBox_.addItem("System default", 1);
    if (auto* type = dm.getCurrentDeviceTypeObject())
        for (const auto& name : type->getDeviceNames(false)) {
            deviceNames_.push_back(name.toStdString());
            deviceBox_.addItem(name, static_cast<int>(deviceNames_.size()));
        }
    auto it = std::find(deviceNames_.begin(), deviceNames_.end(), project.devices.outputDeviceName);
    if (it == deviceNames_.end()) {
        deviceNames_.push_back(project.devices.outputDeviceName);
        deviceBox_.addItem(juce::String(project.devices.outputDeviceName) + " (not available)",
                           static_cast<int>(deviceNames_.size()));
        it = deviceNames_.end() - 1;
    }
    deviceBox_.setSelectedId(static_cast<int>(it - deviceNames_.begin()) + 1, juce::dontSendNotification);

    std::vector<double> deviceRates;
    std::vector<int> deviceSizes;
    if (device != nullptr) {
        for (const double r : device->getAvailableSampleRates()) deviceRates.push_back(r);
        for (const int b : device->getAvailableBufferSizes()) deviceSizes.push_back(b);
    }
    rates_ = settings::sampleRateChoices(deviceRates, project.devices.sampleRate);
    rateBox_.clear(juce::dontSendNotification);
    for (std::size_t i = 0; i < rates_.size(); ++i) {
        rateBox_.addItem(juce::String(settings::formatSampleRate(rates_[i])), static_cast<int>(i) + 1);
        if (std::abs(rates_[i] - project.devices.sampleRate) < 0.5)
            rateBox_.setSelectedId(static_cast<int>(i) + 1, juce::dontSendNotification);
    }
    buffers_ = settings::bufferSizeChoices(deviceSizes, project.devices.bufferSize);
    bufferBox_.clear(juce::dontSendNotification);
    for (std::size_t i = 0; i < buffers_.size(); ++i) {
        bufferBox_.addItem(juce::String(settings::formatBufferSize(buffers_[i], project.devices.sampleRate)),
                           static_cast<int>(i) + 1);
        if (buffers_[i] == project.devices.bufferSize)
            bufferBox_.setSelectedId(static_cast<int>(i) + 1, juce::dontSendNotification);
    }
}

void SettingsPanel::refresh() {
    const juce::ScopedValueSetter<bool> guard(refreshing_, true);
    const auto& p = context_.project();
    refreshChoices();
    speedEdit_.setText(juce::String(p.devices.speedOfSound, 1), false);

    // Virtual device latency (app setting).
    const double rate = p.devices.sampleRate;
    for (std::size_t i = 0; i < std::size(appsettings::kVirtualLatencyChoices); ++i) {
        const int frames = appsettings::kVirtualLatencyChoices[i];
        vdevLatencyBox_.changeItemText(
            static_cast<int>(i) + 1,
            juce::String(frames) + " frames (" + juce::String(statusbar::formatMs(frames, rate)) + ")");
        if (frames == context_.appSettings().virtualDeviceLatencyFrames)
            vdevLatencyBox_.setSelectedId(static_cast<int>(i) + 1, juce::dontSendNotification);
    }

    oscInEdit_.setText(juce::String(p.osc.inPort), false);
    oscOutToggle_.setToggleState(p.osc.out.enabled, juce::dontSendNotification);
    oscHostEdit_.setText(juce::String(p.osc.out.host), false);
    oscOutPortEdit_.setText(juce::String(p.osc.out.port), false);
    oscHostEdit_.setEnabled(p.osc.out.enabled);
    oscOutPortEdit_.setEnabled(p.osc.out.enabled);

    pcBox_.setSelectedId(p.midi.programChangeChannel + 1, juce::dontSendNotification);
    for (std::size_t i = 0; i < midiToggles_.size(); ++i)
        midiToggles_[i]->setToggleState(context_.appSettings().midiInputEnabled(midiNames_[i]),
                                        juce::dontSendNotification);

    for (std::size_t i = 0; i < clockOptions_.size(); ++i)
        if (clockOptions_[i].kind == p.clock.kind)
            clockBox_.setSelectedId(static_cast<int>(i) + 1, juce::dontSendNotification);
    bpmEdit_.setText(juce::String(p.clock.bpm, 1), false);
    bpbEdit_.setText(juce::String(p.clock.beatsPerBar), false);
    trimEdit_.setText(juce::String(p.clock.latencyTrimMs, 1), false);
    quantiseToggle_.setToggleState(p.clock.quantisedStart, juce::dontSendNotification);
    const bool internal = p.clock.kind == ClockKind::Internal;
    bpmEdit_.setEnabled(internal ||
                        p.clock.kind == ClockKind::Mtc);  // MTC derives beats from the tempo (SPEC §9)
    quantiseToggle_.setEnabled(internal);
    trimEdit_.setEnabled(!internal);  // only external clocks are compensated
    clockHint_.setText(juce::String(settings::clockKindHint(p.clock.kind)), juce::dontSendNotification);

    const auto& inst = context_.installationSettings();
    installationToggle_.setToggleState(inst.enabled, juce::dontSendNotification);
    autosaveEdit_.setText(juce::String(inst.autosaveSeconds), false);
    autosaveEdit_.setEnabled(inst.enabled);

    refreshStatus();
}

void SettingsPanel::refreshStatus() {
    const auto s = context_.engine().status();
    if (!context_.deviceError().empty())
        audioStatus_.setText("Device error: " + juce::String(context_.deviceError()),
                             juce::dontSendNotification);
    else
        audioStatus_.setText("Running: " + s.deviceName + ", " +
                                 juce::String(settings::formatSampleRate(s.sampleRate)) + " / " +
                                 juce::String(s.bufferSize) + " frames, " + juce::String(s.outputChannels) +
                                 " outputs",
                             juce::dontSendNotification);

    const auto vdev = context_.virtualDeviceStatus();
    const auto installed = context_.installedDriverVersion();
    juce::String driver = vdev.state == VirtualDeviceReceiver::State::Connected
                              ? "Connected, version " + juce::String(vdev.driverVersion)
                              : juce::String(vdev.message);
    driver << (installed.empty() ? "  (not installed)" : "  (installed: " + juce::String(installed) + ")");
    driverStatus_.setText(driver, juce::dontSendNotification);
    installButton_.setButtonText(installed.empty() ? "Install virtual device..."
                                                   : "Reinstall virtual device...");

    oscStatus_.setText(juce::String(context_.oscBindError()), juce::dontSendNotification);
    oscStatus_.setColour(juce::Label::textColourId, juce::Colour(0xffe0b050));

    const auto configured = context_.project().clock.kind;
    const bool locked = context_.clockInputs().status().locked.load(std::memory_order_relaxed);
    clockStatus_.setText(
        juce::String(settings::clockLockText(configured, context_.activeClockKind(), locked)),
        juce::dontSendNotification);
}

void SettingsPanel::feedbackUpdated() {
    if (--statusCountdown_ > 0) return;
    statusCountdown_ = AppContext::kTickHz / 2;
    refreshStatus();
}

// ---- controls -> model ------------------------------------------------------------

void SettingsPanel::showError(const std::string& message) {
    errorLabel_.setText(juce::String(message), juce::dontSendNotification);
}

void SettingsPanel::commit(const char* undoName) {
    auto fields = settings::fieldsOf(context_.project());
    std::string error;
    auto take = [&](auto parsed, auto& target) {
        if (parsed)
            target = *parsed.value;
        else if (error.empty())
            error = parsed.error;
    };
    if (const int i = comboIndex(deviceBox_); i >= 0 && i < static_cast<int>(deviceNames_.size()))
        fields.devices.outputDeviceName = deviceNames_[static_cast<std::size_t>(i)];
    if (const int i = comboIndex(rateBox_); i >= 0 && i < static_cast<int>(rates_.size()))
        fields.devices.sampleRate = rates_[static_cast<std::size_t>(i)];
    if (const int i = comboIndex(bufferBox_); i >= 0 && i < static_cast<int>(buffers_.size()))
        fields.devices.bufferSize = buffers_[static_cast<std::size_t>(i)];
    take(settings::parseSpeedOfSound(text(speedEdit_)), fields.devices.speedOfSound);
    take(settings::parsePort(text(oscInEdit_)), fields.osc.inPort);
    take(settings::parseHost(text(oscHostEdit_)), fields.osc.out.host);
    take(settings::parsePort(text(oscOutPortEdit_)), fields.osc.out.port);
    fields.osc.out.enabled = oscOutToggle_.getToggleState();
    fields.programChangeChannel = std::clamp(pcBox_.getSelectedId() - 1, 0, 16);
    if (const int i = comboIndex(clockBox_); i >= 0 && i < static_cast<int>(clockOptions_.size()))
        fields.clock.kind = clockOptions_[static_cast<std::size_t>(i)].kind;
    take(settings::parseBpm(text(bpmEdit_)), fields.clock.bpm);
    take(settings::parseBeatsPerBar(text(bpbEdit_)), fields.clock.beatsPerBar);
    take(settings::parseLatencyTrim(text(trimEdit_)), fields.clock.latencyTrimMs);
    fields.clock.quantisedStart = quantiseToggle_.getToggleState();

    if (error.empty()) error = settings::validate(fields);
    if (!error.empty()) {
        refresh();  // back to the current values
        showError(error);
        return;
    }
    showError({});
    if (!settings::classify(settings::fieldsOf(context_.project()), fields).any())
        return;  // nothing changed (e.g. focus lost without editing)
    context_.perform(edits::setSettings(context_.project(), fields, undoName));
}

void SettingsPanel::commitApp() {
    auto s = context_.appSettings();
    if (const int i = comboIndex(vdevLatencyBox_);
        i >= 0 && i < static_cast<int>(std::size(appsettings::kVirtualLatencyChoices)))
        s.virtualDeviceLatencyFrames = appsettings::kVirtualLatencyChoices[i];
    for (std::size_t i = 0; i < midiToggles_.size(); ++i)
        s = appsettings::withMidiInput(std::move(s), midiNames_[i], midiToggles_[i]->getToggleState());
    if (s == context_.appSettings()) return;
    context_.setAppSettings(s);
}

void SettingsPanel::commitInstallation() {
    auto s = context_.installationSettings();
    s.enabled = installationToggle_.getToggleState();
    const auto text = autosaveEdit_.getText().getIntValue();
    if (text >= 1) s.autosaveSeconds = text;
    if (s == context_.installationSettings()) return;
    context_.setInstallationSettings(s);
    refresh();
}

void SettingsPanel::installDriver() {
    installButton_.setEnabled(false);
    context_.installVirtualDevice(
        [safe = juce::Component::SafePointer<SettingsPanel>(this)](std::string error) {
            if (safe == nullptr) return;
            safe->installButton_.setEnabled(true);
            safe->showError(error);
            safe->refreshStatus();
        });
}

void SettingsPanel::createAggregate() {
    const auto all = context_.audioDevices();
    const auto candidates = settings::aggregateCandidates(all);
    auto* alert = new juce::AlertWindow("Create aggregate device",
                                        "Tick the output devices to combine (the first is the clock source).",
                                        juce::MessageBoxIconType::NoIcon, this);
    auto* chooser = new AggregateChooser(candidates);
    alert->addCustomComponent(chooser);
    alert->addButton("Create", 1, juce::KeyPress(juce::KeyPress::returnKey));
    alert->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    alert->enterModalState(
        true,
        juce::ModalCallbackFunction::create(
            [safe = juce::Component::SafePointer<SettingsPanel>(this), chooser, all](int result) {
                if (result != 1 || safe == nullptr) return;
                const auto request = settings::planAggregate(chooser->name(), chooser->chosen(), all);
                const auto error = safe->context_.createAggregateDevice(request);
                safe->showError(error);
                if (error.empty()) safe->refresh();
            }),
        true);
}

// ---- dialog ---------------------------------------------------------------------------

void SettingsPanel::show(AppContext& context) {
    if (gDialog != nullptr) {
        gDialog->toFront(true);
        return;
    }
    juce::DialogWindow::LaunchOptions options;
    // The panel is tall: scroll it inside a window that fits a laptop screen.
    auto* panel = new SettingsPanel(context);
    auto* viewport = new juce::Viewport();
    viewport->setViewedComponent(panel, true);
    viewport->setScrollBarsShown(true, false);
    const int maxHeight = 640;
    const int scrollBar = viewport->getScrollBarThickness();
    viewport->setSize(panel->getWidth() + scrollBar, std::min(panel->getHeight(), maxHeight));
    panel->setSize(viewport->getMaximumVisibleWidth(), panel->getHeight());
    options.content.setOwned(viewport);
    options.dialogTitle = "Settings";
    options.dialogBackgroundColour = juce::Colour(0xff1e2126);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;
    gDialog = options.launchAsync();
}

void SettingsPanel::close() {
    gDialog.deleteAndZero();
}

}  // namespace spacer
