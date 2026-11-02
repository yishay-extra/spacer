#include "control/ControlPath.h"

#include <charconv>
#include <string>

#include "core/motion/ParameterPath.h"

namespace spacer::controlpath {

namespace {

bool parseId(std::string_view s, int& out) {
    if (s.empty()) return false;
    int v = 0;
    const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (ec != std::errc{} || end != s.data() + s.size() || v < 1) return false;
    out = v;
    return true;
}

/// "<id>.<rest>" -> id, rest.
bool splitScoped(std::string_view s, int& id, std::string_view& rest) {
    const auto dot = s.find('.');
    if (dot == std::string_view::npos) return false;
    if (!parseId(s.substr(0, dot), id)) return false;
    rest = s.substr(dot + 1);
    return !rest.empty();
}

}  // namespace

std::optional<ControlEvent> makeEvent(std::string_view path, float value) {
    constexpr std::string_view kSource = "source.";
    constexpr std::string_view kSpeaker = "speaker.";
    int id = 0;
    std::string_view rest;

    if (path.substr(0, kSource.size()) == kSource) {
        if (!splitScoped(path.substr(kSource.size()), id, rest) || !ParameterPath::parse(rest)) return {};
        return ev::SetSourceParam{EntityRef{id, {}}, std::string(rest), value};
    }
    if (path.substr(0, kSpeaker.size()) == kSpeaker) {
        if (!splitScoped(path.substr(kSpeaker.size()), id, rest)) return {};
        if (rest == "trim") return ev::SetSpeakerTrim{EntityRef{id, {}}, value};
        if (rest == "mute") return ev::SetSpeakerMute{EntityRef{id, {}}, value >= 0.5f};
        return {};
    }
    if (path == "master.gain") return ev::SetMasterGain{value};
    if (path == "clock.bpm") return ev::ClockBpm{static_cast<double>(value)};
    if (ParameterPath::parse(path)) return ev::SetSourceParam{EntityRef{0, {}}, std::string(path), value};
    return {};
}

bool isValid(std::string_view path) {
    return makeEvent(path, 0.0f).has_value();
}

}  // namespace spacer::controlpath
