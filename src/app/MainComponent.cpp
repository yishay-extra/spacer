#include "app/MainComponent.h"

namespace spacer {

MainComponent::MainComponent() { setSize(1200, 800); }

void MainComponent::paint(juce::Graphics& g) {
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(24.0f));
    g.drawText("Spacer", getLocalBounds(), juce::Justification::centred, true);
}

void MainComponent::resized() {}

}  // namespace spacer
