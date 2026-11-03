#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <optional>
#include <string>

#include "app/AppContext.h"
#include "ui/FieldList.h"
#include "ui/logic/ProjectEdit.h"

namespace spacer {

/// Horizontal level meter fed by a callback (dBFS), drawn with the same scale
/// as FeedbackState::meterFraction.
class LevelMeter final : public juce::Component {
public:
    std::function<float()> levelDb;
    void paint(juce::Graphics& g) override;
};

/// The strip of one source (SPEC §16 item 2): input, level and mute, spatializer
/// and distance, motion kind with its parameters, modulators, inserts, meter.
/// All content comes from the tables in src/ui/logic; every edit is a
/// ProjectEdit through AppContext::perform(), so it is undoable, and
/// continuous motion parameters reach the engine as SetMotionParam (no phase
/// restart). Plug-in editor windows are WP11e.
class SourceStrip final : public juce::Component {
public:
    explicit SourceStrip(AppContext& context);
    ~SourceStrip() override;

    /// Shows source `sourceId` (0 / unknown: an empty strip).
    void setSource(int sourceId);
    /// Re-reads the model (after any project change).
    void refresh();
    /// Redraws the meter (30 Hz).
    void tick();

    /// Height needed for the current content at `width`.
    int idealHeight(int width);

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    const SourceSpec* source() const;
    bool apply(std::optional<ProjectEdit> edit);
    void browseForFile(const std::string& fieldId);
    void showInsertMenu();
    void layoutContent(int width, bool place);
    int motionChannel() const;

    AppContext& context_;
    int sourceId_{0};
    int channel_{0};

    juce::Label title_;
    LevelMeter meter_;
    juce::Label inputTitle_, levelTitle_, spatialTitle_, motionTitle_, modulatorTitle_, insertTitle_;
    FieldList inputList_, levelList_, spatialList_, motionList_, modulatorList_, insertList_;
    juce::ComboBox motionKind_;
    juce::TextButton channelLeft_{"Left"}, channelRight_{"Right"};
    juce::TextButton addItem_, addModulator_, addInsert_;
    juce::Label insertsHint_;
    std::unique_ptr<juce::FileChooser> chooser_;
    bool refreshing_{false};
    int contentHeight_{80};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SourceStrip)
};

}  // namespace spacer
