#include "app/MainComponent.h"

#include "core/LayoutPresets.h"

namespace spacer {

MainComponent::MainComponent() {
    setSize(1200, 800);

    Project project;
    if (auto layout = layoutpresets::load("stereo")) project.layout = *layout;
    engine_.setProject(project);
    deviceError_ = engine_.openDevice(project.devices);

    startTimerHz(4);
}

MainComponent::~MainComponent() {
    stopTimer();
}

void MainComponent::timerCallback() {
    repaint();
}

void MainComponent::paint(juce::Graphics& g) {
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(24.0f));
    auto area = getLocalBounds();
    g.drawText("Spacer", area.removeFromTop(area.getHeight() / 2), juce::Justification::centredBottom, true);

    const auto s = engine_.status();
    juce::String text;
    if (deviceError_.isNotEmpty())
        text = "Audio device error: " + deviceError_;
    else
        text << s.deviceName << "  ·  " << juce::String(s.sampleRate, 0) << " Hz / " << s.bufferSize
             << "  ·  " << s.outputChannels << " out  ·  latency "
             << (s.deviceLatencySamples + s.graphLatencySamples) << " smp  ·  CPU "
             << juce::String(s.cpuLoad * 100.0, 1) << " %  ·  xruns " << s.xruns;
    g.setFont(juce::FontOptions(14.0f));
    g.drawText(text, area.reduced(20), juce::Justification::centredTop, true);
}

void MainComponent::resized() {}

}  // namespace spacer
