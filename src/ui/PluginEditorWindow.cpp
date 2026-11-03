#include "ui/PluginEditorWindow.h"

namespace spacer {

PluginEditorWindow::PluginEditorWindow(const juce::String& title, std::shared_ptr<HostedPlugin> plugin,
                                       std::function<void()> onCloseRequested)
    : juce::DocumentWindow(title, juce::Colour(0xff25292f),
                           juce::DocumentWindow::minimiseButton | juce::DocumentWindow::closeButton),
      plugin_(std::move(plugin)),
      onCloseRequested_(std::move(onCloseRequested)) {
    setUsingNativeTitleBar(true);
    buildEditor();
    centreWithSize(getWidth(), getHeight());
    setVisible(true);
}

PluginEditorWindow::~PluginEditorWindow() {
    dropEditor();
}

void PluginEditorWindow::buildEditor() {
    if (!plugin_) return;
    auto& instance = plugin_->instance();
    juce::AudioProcessorEditor* editor = nullptr;
    if (instance.hasEditor()) editor = instance.createEditorAndMakeActive();
    if (editor == nullptr) editor = new juce::GenericAudioProcessorEditor(instance);
    setContentOwned(editor, true);  // sizes the window to the editor
    setResizable(editor->isResizable(), false);
}

void PluginEditorWindow::dropEditor() {
    clearContentComponent();  // deletes the editor; the plug-in is released afterwards
}

void PluginEditorWindow::setPlugin(std::shared_ptr<HostedPlugin> plugin) {
    if (plugin == plugin_) return;
    const auto position = getPosition();
    dropEditor();
    plugin_ = std::move(plugin);
    buildEditor();
    setTopLeftPosition(position);
}

void PluginEditorWindow::closeButtonPressed() {
    if (onCloseRequested_) onCloseRequested_();
}

}  // namespace spacer
