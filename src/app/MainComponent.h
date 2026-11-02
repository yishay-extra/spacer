#pragma once

#include <JuceHeader.h>

#include "engine/device/AudioEngine.h"

namespace spacer {

/// Root component of the main window. Placeholder until WP11 (UI) lands: it
/// runs the audio engine on the default output with the stereo preset and
/// shows the engine status.
class MainComponent : public juce::Component, private juce::Timer {
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;

    AudioEngine engine_;
    juce::String deviceError_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};

}  // namespace spacer
