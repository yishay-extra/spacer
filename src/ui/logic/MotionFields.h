#pragma once

// The motion editor's parameter tables (SPEC §10, §16 item 2): for every
// motion kind, which fields the panel shows, how they are formatted, ranges,
// how a typed value is applied to the MotionSpec, which fields can be edited
// live (cmd::SetMotionParam, no phase restart) and the list operations for
// Path points and Step-sequence steps. Plain C++, unit-tested on Linux; the
// JUCE panel just renders `motionFields()` and calls `setMotionField()`.

#include <optional>
#include <string>
#include <vector>

#include "core/Layout.h"
#include "core/MotionSpec.h"
#include "core/motion/MotionParam.h"
#include "ui/logic/ValueFormat.h"

namespace spacer {

enum class MotionKind { Manual, Orbit, Line, Lissajous, Path, RandomWalk, StepSequence };

MotionKind motionKindOf(const MotionSpec& spec);
/// "Manual", "Orbit", "Line", "Lissajous", "Path", "Random walk", "Step sequence".
const char* motionKindName(MotionKind kind);
std::vector<MotionKind> allMotionKinds();
/// A new motion of `kind` with the model's defaults. Manual starts at
/// `startPoint` and Line at `startPoint` too, so switching kind does not make
/// the source jump to an unrelated place when that is avoidable.
MotionSpec makeMotion(MotionKind kind, const Position& startPoint = {0.0f, 0.0f, 1.0f});

enum class FieldType {
    Number,  // a float in `unit`, edited as text / slider
    Period,  // beats or seconds, "8 beats" / "2.5 s"
    Choice,  // one of `choices`
    Toggle,  // on / off
    Text,    // free text (file path, OSC address, name)
};

struct MotionField {
    std::string id;     // stable key, e.g. "radius", "centre.az", "points.2.x", "steps.0.duration"
    std::string label;  // "Radius"
    std::string group;  // section heading ("Centre", "Point 3", …); "" for the top level
    FieldType type{FieldType::Number};

    // Number
    fmt::Unit unit{fmt::Unit::None};
    float value{0.0f};
    float min{0.0f};
    float max{0.0f};
    int decimals{2};  // suggested display precision of value
    // Period
    Period period;
    // Choice
    std::vector<std::string> choices;
    int choice{0};
    // Toggle
    bool on{false};

    /// Current value as text ("1.5 m", "8 beats", "Loop", "on").
    std::string text;
    /// The parameter a modulator / OSC / live edit addresses, for numbers that
    /// have one (SPEC §10 parameter paths).
    std::optional<MotionParam> param;
    /// Index of the Path point / Step this field belongs to, else -1.
    int item{-1};
};

/// The fields to show for `spec`, in display order.
std::vector<MotionField> motionFields(const MotionSpec& spec);

/// Applies `text` to field `fieldId`. Number/Period: parsed with the field's
/// unit (numbers are clamped to the field's range; a Period keeps its unit
/// when only a number is typed); Choice: the choice label (case-insensitive)
/// or its zero-based index; Toggle: 1/0, on/off, true/false, yes/no.
/// False (spec unchanged) for an unknown field or an invalid value. A switch of
/// a Step target to "Speaker" needs `layout` to pick the first speaker.
bool setMotionField(MotionSpec& spec, const Layout& layout, const std::string& fieldId,
                    const std::string& text);

/// The field with this id, if the spec has it.
std::optional<MotionField> findMotionField(const MotionSpec& spec, const std::string& fieldId);

/// Live-editable number: the motion parameter behind field `fieldId`, only for
/// kinds other than Manual (Manual positions already travel as
/// SetSourcePosition). Period, direction, mode and every other structural
/// field return nullopt (SPEC §8: period edits are structural).
std::optional<MotionParam> liveParamOf(const MotionSpec& spec, const std::string& fieldId);

/// Sets motion parameter `param` to `value` (model units). False if the kind
/// has no such parameter.
bool setMotionParamValue(MotionSpec& spec, MotionParam param, float value);

// ---- lists: Path points, Step-sequence steps ------------------------------

/// "Point" for Path, "Step" for StepSequence, "" for kinds without a list.
std::string motionItemName(const MotionSpec& spec);
int motionItemCount(const MotionSpec& spec);
/// Minimum items the model allows (Path 2, steps 1).
int motionItemMinimum(const MotionSpec& spec);
/// Appends a copy of the last item (Path: offset 0.5 m along x; Step: the same
/// step). False if the kind has no list.
bool addMotionItem(MotionSpec& spec);
/// Removes item `index`; false if it does not exist or the minimum would be
/// undercut.
bool removeMotionItem(MotionSpec& spec, int index);

}  // namespace spacer
