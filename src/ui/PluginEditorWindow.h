#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

#include "engine/HostedPlugin.h"

namespace spacer {

/// A window with the editor of one hosted plug-in (SPEC §13, WP11e): the
/// plug-in's own editor, or JUCE's generic parameter editor if it has none.
/// The window owns the editor and shares the instance, so the plug-in is not
/// destroyed under its editor; PluginUi swaps the instance when a graph swap
/// or recall replaces it (setPlugin) and closes the window when its slot goes.
class PluginEditorWindow final : public juce::DocumentWindow {
public:
    PluginEditorWindow(const juce::String& title, std::shared_ptr<HostedPlugin> plugin,
                       std::function<void()> onCloseRequested);
    ~PluginEditorWindow() override;

    /// Shows the editor of `plugin` instead (same window, same position).
    void setPlugin(std::shared_ptr<HostedPlugin> plugin);
    const std::shared_ptr<HostedPlugin>& plugin() const noexcept { return plugin_; }

    void closeButtonPressed() override;

private:
    void buildEditor();
    /// Deletes the editor before the plug-in it belongs to can go.
    void dropEditor();

    std::shared_ptr<HostedPlugin> plugin_;
    std::function<void()> onCloseRequested_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditorWindow)
};

}  // namespace spacer
