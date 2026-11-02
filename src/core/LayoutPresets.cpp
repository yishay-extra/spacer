#include "core/LayoutPresets.h"

#include "LayoutPresetsData.inc"
#include "core/Json.h"

namespace spacer::layoutpresets {

std::vector<std::string> ids() {
    std::vector<std::string> out;
    for (const auto& p : kLayoutPresetSources) out.emplace_back(p.id);
    return out;
}

std::optional<Layout> load(const std::string& id) {
    for (const auto& p : kLayoutPresetSources) {
        if (id != p.id) continue;
        try {
            return Json::parse(p.json).get<Layout>();
        } catch (const std::exception&) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

}  // namespace spacer::layoutpresets
