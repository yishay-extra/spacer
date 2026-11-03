#include "ui/StatusBar.h"

namespace spacer {

namespace {

juce::Colour colourFor(statusbar::Severity s) {
    switch (s) {
        case statusbar::Severity::Warning:
            return juce::Colour(0xffe0b050);
        case statusbar::Severity::Error:
            return juce::Colour(0xffef6f6f);
        case statusbar::Severity::Normal:
            break;
    }
    return juce::Colour(0xffa0a7b0);
}

}  // namespace

StatusBar::StatusBar(AppContext& context) : context_(context) {
    for (auto& label : labels_) {
        label.setFont(juce::FontOptions(12.0f));
        label.setMinimumHorizontalScale(0.8f);
        label.setInterceptsMouseClicks(true, false);
        addAndMakeVisible(label);
    }
    installButton_.setTooltip("Install Spacer.driver, the 64-channel virtual output device (admin password)");
    installButton_.setWantsKeyboardFocus(false);
    installButton_.onClick = [this] {
        if (onInstallDriver) onInstallDriver();
    };
    addChildComponent(installButton_);
    context_.addListener(this);
    update();
}

StatusBar::~StatusBar() {
    context_.removeListener(this);
}

void StatusBar::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff15181c));
    g.setColour(juce::Colour(0xff2a2f36));
    g.drawHorizontalLine(0, 0.0f, static_cast<float>(getWidth()));
}

void StatusBar::resized() {
    auto area = getLocalBounds().reduced(8, 2);
    if (installButton_.isVisible()) {
        installButton_.setBounds(area.removeFromRight(90));
        area.removeFromRight(6);
    }
    // Fixed widths for the short items, the rest goes to the long text of the device, clock and driver.
    const int widths[] = {-1, 70, 70, 120, -1, 0, -1};
    int flexible = 0;
    int fixed = 0;
    for (std::size_t i = 0; i < labels_.size(); ++i) {
        if (widths[i] < 0)
            ++flexible;
        else if (labels_[i].getText().isNotEmpty() || widths[i] > 0)
            fixed += widths[i];
    }
    const int flex = flexible > 0 ? std::max(80, (area.getWidth() - fixed) / flexible) : 0;
    for (std::size_t i = 0; i < labels_.size(); ++i) {
        int w = widths[i] < 0 ? flex : widths[i];
        if (widths[i] == 0) w = labels_[i].getText().isEmpty() ? 0 : 120;  // cue list name
        labels_[i].setBounds(area.removeFromLeft(w));
    }
}

statusbar::Inputs StatusBar::gatherInputs() const {
    statusbar::Inputs in;
    const auto s = context_.engine().status();
    in.deviceError = context_.deviceError();
    in.deviceName = s.deviceName.toStdString();
    in.sampleRate = s.sampleRate;
    in.bufferSize = s.bufferSize;
    in.cpuLoad = s.cpuLoad;
    in.xruns = s.xruns;
    in.deviceLatencySamples = s.deviceLatencySamples;
    in.graphLatencySamples = s.graphLatencySamples;
    in.virtualDeviceLatencySamples = s.virtualDeviceLatencySamples;

    const auto& fb = context_.feedback();
    in.hasClock = fb.hasClock();
    in.clock = fb.clock();
    in.configuredClock = context_.project().clock.kind;
    in.activeClock = context_.activeClockKind();
    in.clockLocked = context_.clockInputs().status().locked.load(std::memory_order_relaxed);

    const auto vdev = context_.virtualDeviceStatus();
    switch (vdev.state) {
        case VirtualDeviceReceiver::State::Absent:
            in.driverState = statusbar::DriverState::Absent;
            break;
        case VirtualDeviceReceiver::State::Incompatible:
            in.driverState = statusbar::DriverState::Incompatible;
            break;
        case VirtualDeviceReceiver::State::NotResponding:
            in.driverState = statusbar::DriverState::NotResponding;
            break;
        case VirtualDeviceReceiver::State::Connected:
            in.driverState = statusbar::DriverState::Connected;
            break;
    }
    in.driverVersion = vdev.driverVersion;
    in.driverWarning = context_.virtualDeviceWarning();
    in.driverUnderrunFrames = vdev.underrunFrames;
    if (context_.cues().armed()) in.armedCueList = context_.cues().sequencer().list().name;
    return in;
}

void StatusBar::update() {
    const auto content = statusbar::compose(gatherInputs());
    const statusbar::Item* items[] = {&content.device, &content.cpu,  &content.xruns, &content.latency,
                                      &content.clock,  &content.cues, &content.driver};
    bool relayout = false;
    for (std::size_t i = 0; i < labels_.size(); ++i) {
        const auto text = juce::String(items[i]->text);
        if (labels_[i].getText() != text) {
            labels_[i].setText(text, juce::dontSendNotification);
            relayout = relayout || i == 5;
        }
        labels_[i].setColour(juce::Label::textColourId, colourFor(items[i]->severity));
        labels_[i].setTooltip(juce::String(items[i]->tooltip));
    }
    if (content.offerDriverInstall != offerInstall_) {
        offerInstall_ = content.offerDriverInstall;
        installButton_.setVisible(offerInstall_);
        relayout = true;
    }
    if (relayout) resized();
}

void StatusBar::feedbackUpdated() {
    if (--countdown_ > 0) return;  // ~4 Hz is plenty
    countdown_ = AppContext::kTickHz / 4;
    update();
}

}  // namespace spacer
