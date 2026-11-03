#include "ui/logic/LayoutEditing.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>

#include "core/LayoutPresets.h"

namespace spacer::layoutedit {

namespace {

std::vector<Speaker>::iterator find(Layout& layout, int id) {
    return std::find_if(layout.speakers.begin(), layout.speakers.end(),
                        [id](const Speaker& s) { return s.id == id; });
}

std::string trim(const std::string& s) {
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

std::optional<float> parseFloat(const std::string& text) {
    const std::string t = trim(text);
    if (t.empty()) return std::nullopt;
    float v = 0.0f;
    const char* first = t.data();
    if (*first == '+') ++first;
    const auto [ptr, ec] = std::from_chars(first, t.data() + t.size(), v);
    if (ec != std::errc() || ptr != t.data() + t.size() || !std::isfinite(v)) return std::nullopt;
    return v;
}

std::optional<int> parseInt(const std::string& text) {
    const std::string t = trim(text);
    if (t.empty()) return std::nullopt;
    int v = 0;
    const auto [ptr, ec] = std::from_chars(t.data(), t.data() + t.size(), v);
    if (ec != std::errc() || ptr != t.data() + t.size()) return std::nullopt;
    return v;
}

std::optional<bool> parseBool(const std::string& text) {
    std::string t = trim(text);
    std::transform(t.begin(), t.end(), t.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (t == "1" || t == "true" || t == "yes" || t == "on" || t == "x") return true;
    if (t == "0" || t == "false" || t == "no" || t == "off" || t.empty()) return false;
    return std::nullopt;
}

std::string formatFloat(float v, int decimals) {
    // Values that round to zero print as "0.0", never "-0.0".
    if (std::abs(v) < 0.5f * std::pow(10.0f, static_cast<float>(-decimals))) v = 0.0f;
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.*f", decimals, static_cast<double>(v));
    return buf;
}

}  // namespace

int nextSpeakerId(const Layout& layout) {
    int m = 0;
    for (const auto& s : layout.speakers) m = std::max(m, s.id);
    return m + 1;
}

int nextFreeOutputChannel(const Layout& layout) {
    for (int ch = 0;; ++ch) {
        const bool used = std::any_of(layout.speakers.begin(), layout.speakers.end(),
                                      [ch](const Speaker& s) { return s.outputChannel == ch; });
        if (!used) return ch;
    }
}

std::optional<int> addSpeaker(Layout& layout, const Position& position) {
    if (layout.speakers.size() >= static_cast<std::size_t>(kMaxSpeakers)) return std::nullopt;
    Speaker s;
    s.id = nextSpeakerId(layout);
    s.name = std::to_string(s.id);
    s.outputChannel = nextFreeOutputChannel(layout);
    s.position = position.normalised();
    s.position.distance = std::max(s.position.distance, kMinSpeakerDistance);
    layout.speakers.push_back(s);
    return s.id;
}

bool removeSpeaker(Layout& layout, int speakerId) {
    auto it = find(layout, speakerId);
    if (it == layout.speakers.end() || layout.speakers.size() <= 2) return false;
    layout.speakers.erase(it);
    return true;
}

std::optional<int> duplicateSpeaker(Layout& layout, int speakerId) {
    auto it = find(layout, speakerId);
    if (it == layout.speakers.end() || layout.speakers.size() >= static_cast<std::size_t>(kMaxSpeakers))
        return std::nullopt;
    Speaker copy = *it;
    copy.id = nextSpeakerId(layout);
    copy.name += " copy";
    copy.outputChannel = nextFreeOutputChannel(layout);
    layout.speakers.insert(it + 1, copy);
    return copy.id;
}

bool moveSpeaker(Layout& layout, int speakerId, const Position& position) {
    auto it = find(layout, speakerId);
    if (it == layout.speakers.end()) return false;
    if (!std::isfinite(position.azimuth) || !std::isfinite(position.elevation) ||
        !std::isfinite(position.distance))
        return false;
    Position p = position.normalised();
    p.distance = std::max(p.distance, kMinSpeakerDistance);
    it->position = p;
    return true;
}

void assignOutputsSequentially(Layout& layout) {
    int ch = 0;
    for (auto& s : layout.speakers) s.outputChannel = ch++;
}

std::optional<Layout> preset(const std::string& presetId) {
    return layoutpresets::load(presetId);
}

std::string formatField(const Speaker& s, SpeakerField field) {
    switch (field) {
        case SpeakerField::Name:
            return s.name;
        case SpeakerField::Azimuth:
            return formatFloat(s.position.azimuth, 1);
        case SpeakerField::Elevation:
            return formatFloat(s.position.elevation, 1);
        case SpeakerField::Distance:
            return formatFloat(s.position.distance, 2);
        case SpeakerField::TrimDb:
            return formatFloat(s.trimDb, 1);
        case SpeakerField::DelayMs:
            return formatFloat(s.userDelayMs, 2);
        case SpeakerField::Polarity:
            return s.polarityInverted ? "inv" : "";
        case SpeakerField::OutputChannel:
            return s.outputChannel >= 0 ? std::to_string(s.outputChannel + 1) : "";
        case SpeakerField::Lfe:
            return s.lfe ? "LFE" : "";
    }
    return {};
}

bool setField(Speaker& s, SpeakerField field, const std::string& text) {
    switch (field) {
        case SpeakerField::Name: {
            const std::string t = trim(text);
            if (t.empty()) return false;
            s.name = t;
            return true;
        }
        case SpeakerField::Azimuth:
            if (auto v = parseFloat(text)) {
                s.position.azimuth = Position::wrapAzimuth(*v);
                return true;
            }
            return false;
        case SpeakerField::Elevation:
            if (auto v = parseFloat(text); v && *v >= -90.0f && *v <= 90.0f) {
                s.position.elevation = *v;
                return true;
            }
            return false;
        case SpeakerField::Distance:
            if (auto v = parseFloat(text); v && *v >= kMinSpeakerDistance) {
                s.position.distance = *v;
                return true;
            }
            return false;
        case SpeakerField::TrimDb:
            if (auto v = parseFloat(text); v && *v >= -60.0f && *v <= 12.0f) {
                s.trimDb = *v;
                return true;
            }
            return false;
        case SpeakerField::DelayMs:
            if (auto v = parseFloat(text); v && *v >= 0.0f && *v <= 100.0f) {
                s.userDelayMs = *v;
                return true;
            }
            return false;
        case SpeakerField::Polarity: {
            std::string t = trim(text);
            if (t == "inv" || t == "-") {
                s.polarityInverted = true;
                return true;
            }
            if (t == "+") {
                s.polarityInverted = false;
                return true;
            }
            if (auto b = parseBool(t)) {
                s.polarityInverted = *b;
                return true;
            }
            return false;
        }
        case SpeakerField::OutputChannel: {
            const std::string t = trim(text);
            if (t.empty() || t == "-") {
                s.outputChannel = -1;
                return true;
            }
            if (auto v = parseInt(t); v && *v >= 0 && *v <= 256) {
                s.outputChannel = *v - 1;
                return true;
            }
            return false;
        }
        case SpeakerField::Lfe: {
            std::string t = trim(text);
            if (t == "LFE" || t == "lfe") {
                s.lfe = true;
                return true;
            }
            if (auto b = parseBool(t)) {
                s.lfe = *b;
                return true;
            }
            return false;
        }
    }
    return false;
}

bool editSpeakerField(Layout& layout, int speakerId, SpeakerField field, const std::string& text) {
    auto it = find(layout, speakerId);
    if (it == layout.speakers.end()) return false;
    Speaker edited = *it;
    if (!setField(edited, field, text) || edited == *it) return false;
    *it = edited;
    return true;
}

std::vector<CompensationRow> compensationRows(const Project& project) {
    const auto comp = computeCompensation(project.layout, project.devices.speedOfSound);
    std::vector<CompensationRow> rows;
    rows.reserve(project.layout.speakers.size());
    for (std::size_t i = 0; i < project.layout.speakers.size(); ++i) {
        const Speaker& sp = project.layout.speakers[i];
        CompensationRow r;
        r.speakerId = sp.id;
        if (i < comp.size()) {
            r.compensationDelayMs = comp[i].delayMs;
            r.compensationGainDb = comp[i].gainDb;
        }
        r.userDelayMs = sp.userDelayMs;
        r.trimDb = sp.trimDb;
        for (const auto& c : project.speakerChains) {
            if (c.speakerId != sp.id) continue;
            r.userDelayMs = c.userDelayMs;
            r.trimDb = c.trimDb;
            r.overriddenByChain = true;
            break;
        }
        rows.push_back(r);
    }
    return rows;
}

TableCell tableCell(const Project& project, const Speaker& speaker, SpeakerField field) {
    TableCell cell;
    cell.text = formatField(speaker, field);
    const bool overridable =
        field == SpeakerField::TrimDb || field == SpeakerField::DelayMs || field == SpeakerField::Polarity;
    if (!overridable) return cell;
    const SpeakerChainSpec* chain = nullptr;
    for (const auto& c : project.speakerChains)
        if (c.speakerId == speaker.id) chain = &c;
    if (chain == nullptr) return cell;
    Speaker effective = speaker;
    effective.trimDb = chain->trimDb;
    effective.userDelayMs = chain->userDelayMs;
    effective.polarityInverted = chain->polarityInverted;
    const std::string chainText = formatField(effective, field);
    cell.text = (chainText.empty() ? std::string("-") : chainText) + " (chain)";
    cell.fromChain = true;
    cell.editable = false;
    const std::string layoutText = formatField(speaker, field);
    cell.tooltip = "Set by this speaker's chain (SPEC 7: the chain overrides the layout); layout value: " +
                   (layoutText.empty() ? std::string("-") : layoutText);
    return cell;
}

}  // namespace spacer::layoutedit
