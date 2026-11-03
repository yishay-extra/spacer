#include "ui/logic/AppSettings.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>

namespace spacer {

bool AppSettings::midiInputEnabled(const std::string& name) const {
    return std::find(disabledMidiInputs.begin(), disabledMidiInputs.end(), name) == disabledMidiInputs.end();
}

namespace appsettings {

int normaliseVirtualLatency(int frames) {
    int best = kVirtualLatencyChoices[0];
    for (const int choice : kVirtualLatencyChoices)
        if (std::abs(choice - frames) < std::abs(best - frames)) best = choice;
    return best;
}

AppSettings withMidiInput(AppSettings settings, const std::string& name, bool enabled) {
    auto& list = settings.disabledMidiInputs;
    list.erase(std::remove(list.begin(), list.end(), name), list.end());
    if (!enabled) list.push_back(name);
    std::sort(list.begin(), list.end());
    return settings;
}

std::string toJsonText(const AppSettings& s) {
    nlohmann::json j;
    j["virtualDeviceLatencyFrames"] = s.virtualDeviceLatencyFrames;
    j["disabledMidiInputs"] = s.disabledMidiInputs;
    return j.dump(2);
}

Expected<AppSettings, Error> parse(const std::string& text) {
    const auto j = nlohmann::json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return Unexpected(Error{"app settings: not a JSON object"});
    AppSettings s;
    try {
        s.virtualDeviceLatencyFrames =
            normaliseVirtualLatency(j.value("virtualDeviceLatencyFrames", kDefaultVirtualLatency));
        s.disabledMidiInputs = j.value("disabledMidiInputs", s.disabledMidiInputs);
    } catch (const nlohmann::json::exception& e) {
        return Unexpected(Error{std::string("app settings: ") + e.what()});
    }
    std::sort(s.disabledMidiInputs.begin(), s.disabledMidiInputs.end());
    s.disabledMidiInputs.erase(std::unique(s.disabledMidiInputs.begin(), s.disabledMidiInputs.end()),
                               s.disabledMidiInputs.end());
    return s;
}

Expected<AppSettings, Error> load(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return Unexpected(Error{"cannot open " + path.string()});
    std::stringstream ss;
    ss << in.rdbuf();
    return parse(ss.str());
}

std::optional<Error> save(const AppSettings& settings, const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    const auto tmp = path.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return Error{"cannot write " + tmp};
        out << toJsonText(settings);
        if (!out) return Error{"write failed: " + tmp};
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) return Error{"cannot rename " + tmp + ": " + ec.message()};
    return std::nullopt;
}

}  // namespace appsettings

}  // namespace spacer
