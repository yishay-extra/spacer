#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

#include "app/AppContext.h"

namespace spacer {

class PluginUi;

/// The plug-in manager (SPEC §13, WP11e): scan, rescan everything, cancel,
/// progress, the scanned plug-ins and the blacklist (plug-ins whose scan
/// crashed, hung or failed, with the reason and a Retry). Scans run on a
/// worker thread (PluginScanJob), each plug-in in its own process, so the
/// window and the audio keep running. All text comes from ui/logic/ScanText.
class PluginManagerWindow final : public juce::DocumentWindow {
public:
    PluginManagerWindow(AppContext& context, PluginUi& ui);
    ~PluginManagerWindow() override;

    /// A scan ended and the host has the new results.
    void scanFinished();
    void closeButtonPressed() override { setVisible(false); }

private:
    class Content;
    Content* content_{nullptr};  // owned by the window

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginManagerWindow)
};

}  // namespace spacer
