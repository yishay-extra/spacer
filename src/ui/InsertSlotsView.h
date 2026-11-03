#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "app/AppContext.h"
#include "core/InsertParamPath.h"
#include "ui/logic/ProjectEdit.h"

namespace spacer {

/// The insert slots of one source or one speaker (SPEC §13): a row per slot
/// with the plug-in's name, bypass, "Edit" (opens its editor window, WP11e)
/// and remove, plus "Add insert..." and "Plug-ins...". Used by the source
/// strip and the layout editor's speaker inspector, so both behave the same.
///
/// The view holds no model: rows come from the project, and every change is
/// an edit built by the owner (its `edits`) and run through
/// AppContext::perform(), so it is undoable.
class InsertSlotsView final : public juce::Component {
public:
    /// How the owner turns a request into an undoable edit (SourceEditing's or
    /// SpeakerChainEditing's builders).
    struct Edits {
        std::function<std::optional<ProjectEdit>(const std::string& pluginId)> add;
        std::function<std::optional<ProjectEdit>(int slot)> remove;
        std::function<std::optional<ProjectEdit>(int slot, bool bypassed)> bypass;
    };

    InsertSlotsView(AppContext& context, Edits edits);
    ~InsertSlotsView() override;

    /// Shows the slots of that owner; ownerId < 0 shows nothing.
    void setOwner(InsertOwner owner, int ownerId);
    /// Re-reads the model (after any project change).
    void refresh();
    /// Cheap check for plug-ins that finished (un)loading since the last refresh (30 Hz).
    void tick();

    int idealHeight() const;
    void resized() override;

private:
    class Row;
    void rebuild();
    void showAddMenu();
    bool apply(std::optional<ProjectEdit> edit);

    AppContext& context_;
    Edits edits_;
    InsertOwner owner_{InsertOwner::Source};
    int ownerId_{-1};

    juce::Label title_;
    juce::Label hint_;
    std::vector<std::unique_ptr<Row>> rows_;
    juce::TextButton addButton_{"Add insert..."};
    juce::TextButton pluginsButton_{"Plug-ins..."};
    std::string signature_;  // what the rows were built from

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InsertSlotsView)
};

}  // namespace spacer
