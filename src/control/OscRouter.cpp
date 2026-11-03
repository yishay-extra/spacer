#include "control/OscRouter.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <string>
#include <vector>

#include "core/InsertParamPath.h"

namespace spacer {

namespace {

using Tokens = std::vector<std::string_view>;

Tokens split(std::string_view s) {
    Tokens out;
    if (!s.empty() && s.front() == '/') s.remove_prefix(1);
    while (true) {
        const auto slash = s.find('/');
        out.push_back(s.substr(0, slash));
        if (slash == std::string_view::npos) break;
        s.remove_prefix(slash + 1);
    }
    return out;
}

/// `{id}`: all digits (>= 1), or a quoted name.
std::optional<EntityRef> parseRef(std::string_view t) {
    if (t.size() >= 2 && t.front() == '"' && t.back() == '"') {
        t = t.substr(1, t.size() - 2);
        if (t.empty()) return {};
        return EntityRef{0, std::string(t)};
    }
    int v = 0;
    const auto [end, ec] = std::from_chars(t.data(), t.data() + t.size(), v);
    if (t.empty() || ec != std::errc{} || end != t.data() + t.size() || v < 1) return {};
    return EntityRef{v, {}};
}

/// Argument accessors: int32 or float32 are interchangeable numbers.
bool isNumber(const juce::OSCArgument& a) {
    return a.isInt32() || a.isFloat32();
}
float number(const juce::OSCArgument& a) {
    return a.isInt32() ? static_cast<float>(a.getInt32()) : a.getFloat32();
}
/// All args are finite numbers; exactly `count` of them.
bool numbers(const juce::OSCMessage& m, int count, float* out) {
    if (m.size() != count) return false;
    for (int i = 0; i < count; ++i) {
        if (!isNumber(m[i])) return false;
        out[i] = number(m[i]);
        if (!std::isfinite(out[i])) return false;
    }
    return true;
}

using R = OscRouter::Result;
R ok(ControlEvent e) {
    return {std::move(e), false};
}
R bad() {
    return {{}, true};
}

R routeSource(const EntityRef& ref, const Tokens& t, const juce::OSCMessage& m, const std::string& address) {
    // t[0..2] = spacer, source, id; the rest starts at t[3].
    const std::string_view a = t.size() > 3 ? t[3] : std::string_view{};
    const std::string_view b = t.size() > 4 ? t[4] : std::string_view{};
    const std::size_t n = t.size();
    float v[3];

    if (n == 5 && a == "pos" && b == "aed") {
        if (!numbers(m, 3, v)) return bad();
        return ok(ev::SetSourcePositionAed{ref, Position{v[0], v[1], v[2]}.normalised()});
    }
    if (n == 5 && a == "pos" && b == "xyz") {
        if (!numbers(m, 3, v)) return bad();
        return ok(ev::SetSourcePositionXyz{ref, Cartesian{v[0], v[1], v[2]}});
    }
    if (n == 4 && (a == "az" || a == "el" || a == "dist" || a == "gain" || a == "spread")) {
        if (!numbers(m, 1, v)) return bad();
        std::string path = a == "az"     ? "motion.position.az"
                           : a == "el"   ? "motion.position.el"
                           : a == "dist" ? "motion.position.dist"
                                         : std::string(a);
        if (a == "spread") v[0] = std::clamp(v[0], 0.0f, 1.0f);
        return ok(ev::SetSourceParam{ref, std::move(path), v[0]});
    }
    if (n == 4 && a == "mute") {
        if (!numbers(m, 1, v)) return bad();
        return ok(ev::SetSourceMute{ref, v[0] >= 0.5f});
    }
    if (n == 5 && a == "motion") {
        if (!numbers(m, 1, v)) return bad();
        return ok(ev::SetSourceParam{ref, "motion." + std::string(b), v[0]});
    }
    if (n == 7 && a == "insert" && t[5] == "param") {
        const std::string tail = "insert." + std::string(b) + ".param." + std::string(t[6]);
        const auto p = InsertParamPath::parseUnscoped(tail);
        if (!p) return ok(ev::OscTrigger{address});
        if (!numbers(m, 1, v)) return bad();
        return ok(ev::SetInsertParam{InsertOwner::Source, ref, p->slot, p->index, v[0]});
    }
    if (n == 5 && a == "file" && (b == "play" || b == "stop")) {
        if (m.size() != 0) return bad();
        return ok(ev::SourceFile{
            ref, b == "play" ? ev::SourceFile::Action::Play : ev::SourceFile::Action::Stop, 0.0});
    }
    if (n == 5 && a == "file" && b == "seek") {
        if (!numbers(m, 1, v) || v[0] < 0.0f) return bad();
        return ok(ev::SourceFile{ref, ev::SourceFile::Action::Seek, static_cast<double>(v[0])});
    }
    return ok(ev::OscTrigger{address});
}

R routeSpeaker(const EntityRef& ref, const Tokens& t, const juce::OSCMessage& m, const std::string& address) {
    float v[1];
    if (t.size() == 4 && t[3] == "trim") {
        if (!numbers(m, 1, v)) return bad();
        return ok(ev::SetSpeakerTrim{ref, v[0]});
    }
    if (t.size() == 4 && t[3] == "mute") {
        if (!numbers(m, 1, v)) return bad();
        return ok(ev::SetSpeakerMute{ref, v[0] >= 0.5f});
    }
    if (t.size() == 7 && t[3] == "insert" && t[5] == "param") {
        const auto p =
            InsertParamPath::parseUnscoped("insert." + std::string(t[4]) + ".param." + std::string(t[6]));
        if (!p) return ok(ev::OscTrigger{address});
        if (!numbers(m, 1, v)) return bad();
        return ok(ev::SetInsertParam{InsertOwner::Speaker, ref, p->slot, p->index, v[0]});
    }
    return ok(ev::OscTrigger{address});
}

}  // namespace

OscRouter::Result OscRouter::route(const juce::OSCMessage& m) {
    const std::string address = m.getAddressPattern().toString().toStdString();
    const Tokens t = split(address);
    float v[2];

    if (t.size() < 2 || t[0] != "spacer") return ok(ev::OscTrigger{address});
    const std::string_view what = t[1];

    if (what == "source" && t.size() >= 4) {
        const auto ref = parseRef(t[2]);
        return ref ? routeSource(*ref, t, m, address) : ok(ev::OscTrigger{address});
    }
    if (what == "speaker" && t.size() >= 4) {
        const auto ref = parseRef(t[2]);
        return ref ? routeSpeaker(*ref, t, m, address) : ok(ev::OscTrigger{address});
    }
    if (t.size() == 3 && what == "master" && t[2] == "gain") {
        if (!numbers(m, 1, v)) return bad();
        return ok(ev::SetMasterGain{v[0]});
    }
    if (t.size() == 2 && what == "panic") {
        // No argument: engage. Optional int 0 releases (extension, see PR notes).
        if (m.size() == 0) return ok(ev::Panic{true});
        if (!numbers(m, 1, v)) return bad();
        return ok(ev::Panic{v[0] >= 0.5f});
    }
    if (what == "cue" && t.size() == 3) {
        if (t[2] == "go" || t[2] == "back") {
            if (m.size() != 0) return bad();
            return t[2] == "go" ? ok(ev::CueGo{}) : ok(ev::CueBack{});
        }
        if (t[2] == "jump") {
            if (m.size() != 1) return bad();
            if (m[0].isInt32()) return ok(ev::CueJump{m[0].getInt32(), {}});
            if (m[0].isString() && m[0].getString().isNotEmpty())
                return ok(ev::CueJump{0, m[0].getString().toStdString()});
            return bad();
        }
    }
    if (what == "scene" && t.size() == 3 && t[2] == "recall") {
        if ((m.size() != 1 && m.size() != 2) || !m[0].isString() || m[0].getString().isEmpty()) return bad();
        float seconds = 0.0f;
        if (m.size() == 2) {
            if (!isNumber(m[1])) return bad();
            seconds = number(m[1]);
            if (!std::isfinite(seconds) || seconds < 0.0f) return bad();
        }
        return ok(ev::RecallScene{m[0].getString().toStdString(), seconds});
    }
    if (what == "clock" && t.size() == 3) {
        if (t[2] == "bpm") {
            if (!numbers(m, 1, v) || v[0] <= 0.0f) return bad();
            return ok(ev::ClockBpm{static_cast<double>(v[0])});
        }
        if (t[2] == "beat") {
            if (!numbers(m, 1, v)) return bad();
            return ok(ev::ClockBeat{static_cast<double>(v[0])});
        }
        if (t[2] == "play" || t[2] == "stop" || t[2] == "continue") {
            if (m.size() != 0) return bad();
            if (t[2] == "play") return ok(ev::ClockPlay{});
            if (t[2] == "stop") return ok(ev::ClockStop{});
            return ok(ev::ClockContinue{});
        }
    }
    return ok(ev::OscTrigger{address});
}

}  // namespace spacer
