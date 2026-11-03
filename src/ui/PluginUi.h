#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <string>
#include <vector>

#include "app/AppContext.h"
#include "engine/PluginScanJob.h"
#include "ui/logic/PluginWindowBook.h"

namespace spacer {

class PluginEditorWindow;
class PluginManagerWindow;

/// Plug-in UI of the app (SPEC §13, WP11e), owned by AppContext:
///
///  - editor windows: one per insert slot, opened from the strip or the
///    speaker inspector. The slot <-> window bookkeeping is WindowBook; the
///    windows follow the instance PluginHost holds for the slot across graph
///    swaps and recalls, and close when the slot or its plug-in goes away.
///    Edits made in an editor are written to the model by
///    PluginHost::captureState (save, scene update, before a recall).
///  - the background plug-in scan (PluginScanJob) and the plug-in manager
///    window (scan / rescan / cancel, progress, blacklist).
///
/// Message thread only.
class PluginUi final : private AppContext::Listener {
public:
    explicit PluginUi(AppContext& context);
    ~PluginUi() override;

    /// Opens (or raises) the editor window of an insert slot. Returns why it
    /// cannot be opened (empty slot, plug-in missing or failed to load), or ""
    /// on success.
    std::string openEditor(InsertOwner owner, int ownerId, int slot);
    bool isEditorOpen(InsertOwner owner, int ownerId, int slot) const;
    int numEditorWindows() const noexcept { return static_cast<int>(windows_.size()); }
    void closeAllEditors();

    PluginScanJob& scanJob() noexcept { return scan_; }
    /// Starts a background scan (`rescanAll`: also cached and blacklisted ones).
    /// False if one is already running.
    bool startScan(bool rescanAll);
    /// Increments whenever a scan ended and its results were adopted.
    unsigned scanGeneration() const noexcept { return scanGeneration_; }
    void showPluginManager();

private:
    struct Window {
        pluginwindows::SlotKey key;
        std::unique_ptr<PluginEditorWindow> window;
    };

    void projectChanged(AppContext::Change) override;
    void feedbackUpdated() override;

    pluginwindows::InstanceLookup lookup();
    void reconcile();
    Window* windowFor(const pluginwindows::SlotKey& key);
    juce::String titleFor(const pluginwindows::SlotKey& key) const;

    AppContext& context_;
    std::shared_ptr<bool> alive_{std::make_shared<bool>(true)};  // for deferred callbacks
    pluginwindows::WindowBook book_;
    std::vector<Window> windows_;
    PluginScanJob scan_;
    unsigned scanGeneration_{0};
    std::unique_ptr<PluginManagerWindow> manager_;
};

}  // namespace spacer
