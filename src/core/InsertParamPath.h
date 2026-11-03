#pragma once

// Plugin parameter paths (SPEC §13): `source.{id}.insert.{slot}.param.{index}`.
// Speaker inserts use the same shape under `speaker.{id}` (SPEC §13 names
// only the source form; the speaker form is its natural counterpart).
// `slot` is 0-based (index into the inserts list), `index` is the plugin's
// parameter index; values are normalised 0..1, as in PluginSlot::params.

#include <optional>
#include <string>
#include <string_view>

namespace spacer {

enum class InsertOwner { Source, Speaker };

struct InsertParamPath {
    InsertOwner owner{InsertOwner::Source};
    int ownerId{0};  // >= 1 for a scoped path, 0 when parsed unscoped
    int slot{0};     // 0..kMaxInserts-1
    int index{0};    // >= 0

    /// "source.3.insert.0.param.12" / "speaker.2.insert.1.param.0". Does not allocate.
    static std::optional<InsertParamPath> parse(std::string_view path);
    /// The unscoped tail "insert.{slot}.param.{index}" (ownerId stays 0).
    static std::optional<InsertParamPath> parseUnscoped(std::string_view path);

    std::string toString() const;
    bool operator==(const InsertParamPath&) const = default;
};

}  // namespace spacer
