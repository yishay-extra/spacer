#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ui/logic/MotionFields.h"

namespace spacer {

/// A vertical list of editable parameter rows built from field descriptors
/// (the tables in src/ui/logic: MotionFields, SourceEditing). All semantics —
/// ranges, units, parsing — live in the tables; this only draws a control per
/// field type and reports typed values as text:
///
///   Number  -> bar slider (drag, or double-click and type) in the field's unit
///   Period  -> editable label ("8 beats", "2.5 s")
///   Text    -> editable label
///   Choice  -> combo box
///   Toggle  -> check box
///
/// Rows are reused while the field ids stay the same, so a refresh after an
/// edit does not disturb a drag in progress.
class FieldList final : public juce::Component {
public:
    static constexpr int kRowHeight = 24;
    static constexpr int kHeaderHeight = 22;

    FieldList();
    ~FieldList() override;

    /// Replaces the shown fields. Rows with unchanged ids are updated in place.
    void setFields(std::vector<MotionField> fields);

    /// Height needed to show every row.
    int idealHeight() const;

    /// A field was edited: its id and the new value as text. Returns true if
    /// the edit was applied; on false the row shows the model's value again.
    std::function<bool(const std::string& id, const std::string& text)> onEdit;
    /// A slider drag started / ended (brackets one undo step).
    std::function<void()> onGestureBegin;
    std::function<void()> onGestureEnd;
    /// If set, rows that belong to a list item (Path point, Step, modulator)
    /// get a remove button that calls this with the item index.
    std::function<void(int item)> onRemoveItem;
    /// If set, called for the "..." button of text field `id` (file chooser).
    std::function<bool(const std::string& id)> wantsBrowse;
    std::function<void(const std::string& id)> onBrowse;

    void resized() override;

private:
    class Row;
    struct Entry {
        std::string headerText;  // non-empty: a section heading before the row
        std::unique_ptr<Row> row;
    };

    std::vector<Entry> entries_;
    std::vector<std::string> ids_;
    std::vector<MotionField> fields_;
    std::vector<std::unique_ptr<juce::Label>> headers_;

    void rebuild();
    void updateRows();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FieldList)
};

}  // namespace spacer
