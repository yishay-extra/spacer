#include "ui/logic/TriggerText.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <vector>

namespace spacer::triggertext {

namespace {

constexpr const char* kDays[7] = {"sun", "mon", "tue", "wed", "thu", "fri", "sat"};

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::vector<std::string> tokens(const std::string& text) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : text) {
        if (std::isspace(static_cast<unsigned char>(c)) || c == ',') {
            if (!cur.empty()) out.push_back(std::move(cur));
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) out.push_back(std::move(cur));
    return out;
}

bool parseDouble(const std::string& s, double& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    out = std::strtod(s.c_str(), &end);
    return end == s.c_str() + s.size() && std::isfinite(out);
}

bool parseInt(const std::string& s, int& out) {
    if (s.empty() || s.size() > 6) return false;
    for (char c : s)
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    out = std::atoi(s.c_str());
    return true;
}

/// "4s" -> ("4", "s"); "2.5" -> ("2.5", "").
void splitSuffix(const std::string& t, std::string& number, std::string& suffix) {
    std::size_t i = 0;
    while (i < t.size() && (std::isdigit(static_cast<unsigned char>(t[i])) || t[i] == '.')) ++i;
    number = t.substr(0, i);
    suffix = t.substr(i);
}

std::string num(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.3f", v);
    std::string s = buf;
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    return s;
}

std::optional<int> dayIndex(const std::string& t) {
    if (t.size() < 3) return std::nullopt;
    for (int i = 0; i < 7; ++i)
        if (t.compare(0, 3, kDays[i]) == 0) return i;
    return std::nullopt;
}

std::optional<Trigger> parseAtBeat(const std::vector<std::string>& t) {
    int bar = 0;
    double beat = 1.0;
    if (t[0] == "bar") {
        // bar N [beat X]
        if (t.size() != 2 && t.size() != 4) return std::nullopt;
        if (!parseInt(t[1], bar)) return std::nullopt;
        if (t.size() == 4 && (t[2] != "beat" || !parseDouble(t[3], beat))) return std::nullopt;
    } else {
        return std::nullopt;
    }
    if (bar < 1 || bar > kMaxBar || beat < 1.0 || beat >= kMaxBeat) return std::nullopt;
    return Trigger{trigger::AtBeat{bar, beat}};
}

std::optional<Trigger> parseAfter(const std::vector<std::string>& t) {
    // after N [unit] [+R]
    std::size_t i = 1;
    if (i >= t.size()) return std::nullopt;
    std::string number, suffix;
    splitSuffix(t[i++], number, suffix);
    double value = 0.0;
    if (!parseDouble(number, value)) return std::nullopt;
    if (suffix.empty() && i < t.size() && t[i][0] != '+') suffix = t[i++];
    Period::Unit unit = Period::Unit::Seconds;
    if (suffix.empty() || suffix == "s" || suffix == "sec" || suffix == "secs" || suffix == "second" ||
        suffix == "seconds")
        unit = Period::Unit::Seconds;
    else if (suffix == "b" || suffix == "beat" || suffix == "beats")
        unit = Period::Unit::Beats;
    else
        return std::nullopt;
    double range = 0.0;
    if (i < t.size()) {
        std::string r = t[i++];
        if (r[0] != '+') return std::nullopt;
        r.erase(0, 1);
        if (r.empty() && i < t.size()) r = t[i++];
        std::string rn, rs;
        splitSuffix(r, rn, rs);
        if (!parseDouble(rn, range) || range < 0.0) return std::nullopt;
        if (!rs.empty() && rs != "s" && rs != "b") return std::nullopt;  // the unit is the delay's
    }
    if (i != t.size()) return std::nullopt;
    const double maxV = unit == Period::Unit::Beats ? kMaxDelayBeats : kMaxDelaySeconds;
    if (value < 0.0 || value > maxV || range > maxV) return std::nullopt;
    return Trigger{trigger::AfterPrevious{{unit, value}, range}};
}

std::optional<Trigger> parseMidi(const std::vector<std::string>& t) {
    trigger::Midi m;
    m.type = t[0] == "note" ? trigger::Midi::Type::Note : trigger::Midi::Type::Cc;
    if (t.size() < 2 || !parseInt(t[1], m.number) || m.number > 127) return std::nullopt;
    if (t.size() == 2) return Trigger{m};
    std::string ch;
    if (t[2] == "ch" || t[2] == "channel") {
        if (t.size() != 4) return std::nullopt;
        ch = t[3];
    } else if (t.size() == 3 && t[2].rfind("ch", 0) == 0) {
        ch = t[2].substr(2);
    } else {
        return std::nullopt;
    }
    if (!parseInt(ch, m.channel) || m.channel < 1 || m.channel > 16) return std::nullopt;
    return Trigger{m};
}

std::optional<Trigger> parseTimeOfDay(const std::vector<std::string>& t) {
    const auto colon = t[0].find(':');
    trigger::TimeOfDay tod;
    if (!parseInt(t[0].substr(0, colon), tod.hour) || !parseInt(t[0].substr(colon + 1), tod.minute))
        return std::nullopt;
    if (tod.hour > 23 || tod.minute > 59) return std::nullopt;
    std::set<int> days;
    for (std::size_t i = 1; i < t.size(); ++i) {
        if (t[i] == "daily" || t[i] == "everyday") {
            days.clear();
            if (t.size() != 2) return std::nullopt;
            break;
        }
        const auto dash = t[i].find('-');
        if (dash == std::string::npos) {
            const auto d = dayIndex(t[i]);
            if (!d) return std::nullopt;
            days.insert(*d);
        } else {
            const auto a = dayIndex(t[i].substr(0, dash));
            const auto b = dayIndex(t[i].substr(dash + 1));
            if (!a || !b) return std::nullopt;
            for (int d = *a;; d = (d + 1) % 7) {  // wraps: "fri-mon"
                days.insert(d);
                if (d == *b) break;
            }
        }
    }
    if (days.size() == 7) days.clear();  // every day
    tod.days.assign(days.begin(), days.end());
    return Trigger{tod};
}

}  // namespace

Kind kindOf(const Trigger& trigger) {
    return static_cast<Kind>(trigger.value.index());
}

const char* kindName(Kind kind) {
    switch (kind) {
        case Kind::Manual:
            return "Manual";
        case Kind::AtBeat:
            return "At beat";
        case Kind::AfterPrevious:
            return "After previous";
        case Kind::Osc:
            return "OSC";
        case Kind::Midi:
            return "MIDI";
        case Kind::TimeOfDay:
            return "Time of day";
    }
    return "";
}

const char* dayName(int day) {
    static const char* names[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    return day >= 0 && day < 7 ? names[day] : "";
}

Trigger defaultTrigger(Kind kind) {
    switch (kind) {
        case Kind::Manual:
            return Trigger{trigger::Manual{}};
        case Kind::AtBeat:
            return Trigger{trigger::AtBeat{}};
        case Kind::AfterPrevious:
            return Trigger{trigger::AfterPrevious{Period::seconds(4), 0.0}};
        case Kind::Osc:
            return Trigger{trigger::Osc{"/cue"}};
        case Kind::Midi:
            return Trigger{trigger::Midi{}};
        case Kind::TimeOfDay:
            return Trigger{trigger::TimeOfDay{}};
    }
    return {};
}

std::string format(const Trigger& trigger) {
    struct V {
        std::string operator()(const trigger::Manual&) const { return "manual"; }
        std::string operator()(const trigger::AtBeat& t) const {
            return "bar " + std::to_string(t.bar) + " beat " + num(t.beat);
        }
        std::string operator()(const trigger::AfterPrevious& t) const {
            const std::string unit = t.delay.unit == Period::Unit::Beats ? " beats" : " s";
            std::string s = "after " + num(t.delay.value) + unit;
            if (t.randomRange > 0.0) s += " +" + num(t.randomRange);
            return s;
        }
        std::string operator()(const trigger::Osc& t) const { return "osc " + t.address; }
        std::string operator()(const trigger::Midi& t) const {
            std::string s =
                std::string(t.type == trigger::Midi::Type::Note ? "note " : "cc ") + std::to_string(t.number);
            if (t.channel != 0) s += " ch" + std::to_string(t.channel);
            return s;
        }
        std::string operator()(const trigger::TimeOfDay& t) const {
            char buf[16];
            std::snprintf(buf, sizeof buf, "%02d:%02d", t.hour, t.minute);
            std::string s = buf;
            std::set<int> days(t.days.begin(), t.days.end());
            bool first = true;
            for (int d : days) {
                s += first ? " " : ",";
                s += dayName(d);
                first = false;
            }
            return s;
        }
    };
    return std::visit(V{}, trigger.value);
}

std::optional<Trigger> parse(const std::string& text) {
    // OSC addresses are case-sensitive: split off the address before lowering.
    const auto raw = tokens(text);
    if (raw.empty()) return std::nullopt;
    if (lower(raw[0]) == "osc") {
        if (raw.size() != 2 || raw[1][0] != '/' || raw[1].size() < 2) return std::nullopt;
        return Trigger{trigger::Osc{raw[1]}};
    }
    std::vector<std::string> t;
    for (const auto& r : raw) t.push_back(lower(r));

    if (t[0] == "manual" || t[0] == "go")
        return t.size() == 1 ? std::optional<Trigger>{Trigger{}} : std::nullopt;
    if (t[0] == "after") return parseAfter(t);
    if (t[0] == "bar") return parseAtBeat(t);
    if (t[0] == "note" || t[0] == "cc") return parseMidi(t);
    if (t[0].find(':') != std::string::npos) return parseTimeOfDay(t);
    return std::nullopt;
}

}  // namespace spacer::triggertext
