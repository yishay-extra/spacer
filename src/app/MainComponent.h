#pragma once

#include <JuceHeader.h>

namespace spacer {

/// Root component of the main window. Placeholder until WP11 (UI) lands.
class MainComponent : public juce::Component {
public:
    MainComponent();

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};

}  // namespace spacer
