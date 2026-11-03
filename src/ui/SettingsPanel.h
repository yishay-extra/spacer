#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

#include "app/AppContext.h"
#include "ui/logic/SettingsModel.h"

namespace spacer {

/// Settings dialog (SPEC §16.5): audio device / create aggregate, sample
/// rate, buffer size, speed of sound, virtual device (driver status, install,
/// latency), OSC ports, MIDI inputs and program-change channel, clock source
/// with lock status, installation mode.
///
/// Project-stored values change through one undoable edit
/// (edits::setSettings); AppContext applies what it needs (device reopen,
/// graph rebuild with the new clock, OSC / MIDI re-bind). App-only values go
/// to AppContext::setAppSettings. Invalid text is rejected with a message and
/// the field returns to the current value.
class SettingsPanel final : public juce::Component, private AppContext::Listener {
public:
    static constexpr int kWidth = 600;

    explicit SettingsPanel(AppContext& context);
    ~SettingsPanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    /// Opens the (modal) dialog, or brings it to the front.
    static void show(AppContext& context);
    /// Closes it if open (the app is shutting down).
    static void close();

private:
    void projectChanged(AppContext::Change) override { refresh(); }
    void feedbackUpdated() override;

    void refresh();
    void refreshChoices();
    void refreshStatus();
    /// Reads the controls into the project's settings fields and performs the edit.
    void commit(const char* undoName);
    void commitApp();
    void commitInstallation();
    void createAggregate();
    void installDriver();
    void showError(const std::string& message);

    struct Row {
        juce::Label* label;
        std::vector<juce::Component*> controls;
        int height{24};
        bool header{false};
    };

    AppContext& context_;
    bool refreshing_{false};
    int statusCountdown_{0};

    // Audio.
    juce::Label deviceLabel_{{}, "Output device"};
    juce::ComboBox deviceBox_;
    juce::Label rateLabel_{{}, "Sample rate"};
    juce::ComboBox rateBox_;
    juce::Label bufferLabel_{{}, "Buffer size"};
    juce::ComboBox bufferBox_;
    juce::Label speedLabel_{{}, "Speed of sound (m/s)"};
    juce::TextEditor speedEdit_;
    juce::TextButton aggregateButton_{"Create aggregate device..."};
    juce::Label audioStatus_;
    // Virtual device.
    juce::Label driverLabel_{{}, "Spacer.driver"};
    juce::Label driverStatus_;
    juce::TextButton installButton_{"Install virtual device..."};
    juce::Label vdevLatencyLabel_{{}, "Virtual device latency"};
    juce::ComboBox vdevLatencyBox_;
    // OSC.
    juce::Label oscInLabel_{{}, "OSC input port"};
    juce::TextEditor oscInEdit_;
    juce::ToggleButton oscOutToggle_{"OSC output"};
    juce::TextEditor oscHostEdit_;
    juce::TextEditor oscOutPortEdit_;
    juce::Label oscStatus_;
    // MIDI.
    juce::Label midiInputsLabel_{{}, "MIDI inputs"};
    juce::Label pcLabel_{{}, "Program change channel"};
    juce::ComboBox pcBox_;
    std::vector<std::unique_ptr<juce::ToggleButton>> midiToggles_;
    std::vector<std::string> midiNames_;
    // Clock.
    juce::Label clockLabel_{{}, "Clock source"};
    juce::ComboBox clockBox_;
    juce::Label clockStatus_;
    juce::Label bpmLabel_{{}, "Tempo (bpm)"};
    juce::TextEditor bpmEdit_;
    juce::Label bpbLabel_{{}, "Beats per bar"};
    juce::TextEditor bpbEdit_;
    juce::ToggleButton quantiseToggle_{"Start on next bar"};
    juce::Label clockHint_;
    // Installation mode.
    juce::ToggleButton installationToggle_{"Installation mode (reopen last project, start the show)"};
    juce::Label autosaveLabel_{{}, "Autosave every (s)"};
    juce::TextEditor autosaveEdit_;

    juce::Label errorLabel_;
    juce::Label headerAudio_{{}, "Audio"}, headerVdev_{{}, "Virtual device"}, headerOsc_{{}, "OSC"},
        headerMidi_{{}, "MIDI"}, headerClock_{{}, "Clock"}, headerInstall_{{}, "Installation"};

    std::vector<std::string> deviceNames_;  // parallel to deviceBox_ items ("" = system default)
    std::vector<double> rates_;
    std::vector<int> buffers_;
    std::vector<settings::ClockOption> clockOptions_;
    std::vector<Row> rows_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsPanel)
};

}  // namespace spacer
