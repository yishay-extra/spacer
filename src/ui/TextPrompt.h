#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace spacer {

/// Asks for one line of text in a modal alert. `onOk` runs with the text if
/// the user confirms; nothing happens on Cancel. Asynchronous (the window
/// deletes itself).
inline void promptForText(const juce::String& title, const juce::String& label, const juce::String& initial,
                          std::function<void(const juce::String&)> onOk) {
    auto* window = new juce::AlertWindow(title, label, juce::MessageBoxIconType::NoIcon);
    window->addTextEditor("text", initial);
    window->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    juce::Component::SafePointer<juce::AlertWindow> safe(window);
    window->enterModalState(
        true, juce::ModalCallbackFunction::create([safe, callback = std::move(onOk)](int result) {
            if (result == 1 && safe != nullptr && callback)
                callback(safe->getTextEditorContents("text").trim());
        }),
        true);
}

}  // namespace spacer
