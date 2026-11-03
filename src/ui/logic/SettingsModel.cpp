#include "ui/logic/SettingsModel.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>

namespace spacer::settings {

namespace {

std::string trim(const std::string& s) {
    std::size_t a = 0;
    std::size_t b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::optional<double> toDouble(const std::string& text) {
    const auto t = trim(text);
    if (t.empty()) return std::nullopt;
    char* end = nullptr;
    const double v = std::strtod(t.c_str(), &end);
    if (end != t.c_str() + t.size() || !std::isfinite(v)) return std::nullopt;
    return v;
}

std::optional<int> toInt(const std::string& text) {
    const auto t = trim(text);
    int v = 0;
    const auto [ptr, ec] = std::from_chars(t.data(), t.data() + t.size(), v);
    if (t.empty() || ec != std::errc() || ptr != t.data() + t.size()) return std::nullopt;
    return v;
}

template <typename T>
Parsed<T> fail(std::string message) {
    return {std::nullopt, std::move(message)};
}

std::string number(double v, int decimals) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.*f", decimals, v);
    std::string s = buf;
    if (decimals > 0 && s.find('.') != std::string::npos) {
        while (s.back() == '0') s.pop_back();
        if (s.back() == '.') s.pop_back();
    }
    return s;
}

}  // namespace

std::vector<double> sampleRateChoices(const std::vector<double>& deviceRates, double current) {
    std::set<double> out;
    for (const double standard : kStandardSampleRates) {
        const bool supported =
            deviceRates.empty() || std::any_of(deviceRates.begin(), deviceRates.end(),
                                               [&](double r) { return std::abs(r - standard) < 0.5; });
        if (supported) out.insert(standard);
    }
    if (current > 0.0 &&
        std::none_of(out.begin(), out.end(), [&](double r) { return std::abs(r - current) < 0.5; }))
        out.insert(current);
    return {out.begin(), out.end()};
}

std::vector<int> bufferSizeChoices(const std::vector<int>& deviceSizes, int current) {
    std::set<int> out;
    if (deviceSizes.empty()) {
        for (int n = kMinBufferSize; n <= kMaxBufferSize; n *= 2) out.insert(n);
    } else {
        for (const int n : deviceSizes)
            if (n >= kMinBufferSize && n <= kMaxBufferSize) out.insert(n);
    }
    if (current > 0) out.insert(current);
    return {out.begin(), out.end()};
}

std::string formatSampleRate(double hz) {
    return number(hz / 1000.0, 1) + " kHz";
}

std::string formatBufferSize(int frames, double sampleRate) {
    if (sampleRate <= 0.0) return std::to_string(frames);
    return std::to_string(frames) + " (" + number(1000.0 * frames / sampleRate, 1) + " ms)";
}

Parsed<int> parsePort(const std::string& text) {
    const auto v = toInt(text);
    if (!v) return fail<int>("Enter a port number");
    if (*v < 1 || *v > 65535) return fail<int>("Port must be within 1 to 65535");
    return {*v, {}};
}

Parsed<std::string> parseHost(const std::string& text) {
    const auto t = trim(text);
    if (t.empty()) return fail<std::string>("Enter a host name or IP address");
    if (std::any_of(t.begin(), t.end(), [](char c) { return std::isspace(static_cast<unsigned char>(c)); }))
        return fail<std::string>("A host name cannot contain spaces");
    return {t, {}};
}

Parsed<double> parseBpm(const std::string& text) {
    const auto v = toDouble(text);
    if (!v) return fail<double>("Enter a tempo in bpm");
    if (*v < kMinBpm || *v > kMaxBpm) return fail<double>("Tempo must be within 20 to 300 bpm");
    return {*v, {}};
}

Parsed<int> parseBeatsPerBar(const std::string& text) {
    const auto v = toInt(text);
    if (!v) return fail<int>("Enter the beats per bar");
    if (*v < 1 || *v > kMaxBeatsPerBar) return fail<int>("Beats per bar must be within 1 to 32");
    return {*v, {}};
}

Parsed<float> parseLatencyTrim(const std::string& text) {
    const auto v = toDouble(text);
    if (!v) return fail<float>("Enter the clock latency trim in ms");
    if (*v < -1000.0 || *v > 1000.0) return fail<float>("Clock latency trim must be within -1000 to 1000 ms");
    return {static_cast<float>(*v), {}};
}

Parsed<float> parseSpeedOfSound(const std::string& text) {
    const auto v = toDouble(text);
    if (!v) return fail<float>("Enter the speed of sound in m/s");
    if (*v < kMinSpeedOfSound || *v > kMaxSpeedOfSound)
        return fail<float>("Speed of sound must be within 300 to 400 m/s");
    return {static_cast<float>(*v), {}};
}

SettingsFields fieldsOf(const Project& p) {
    return {p.devices, p.osc, p.midi.programChangeChannel, p.clock};
}

void apply(Project& p, const SettingsFields& f) {
    p.devices = f.devices;
    p.osc = f.osc;
    p.midi.programChangeChannel = f.programChangeChannel;
    p.clock = f.clock;
}

Delta classify(const SettingsFields& a, const SettingsFields& b) {
    Delta d;
    d.device = a.devices.outputDeviceName != b.devices.outputDeviceName ||
               a.devices.sampleRate != b.devices.sampleRate || a.devices.bufferSize != b.devices.bufferSize;
    ClockConfig clockA = a.clock;
    clockA.latencyTrimMs = b.clock.latencyTrimMs;  // read live by the app tick
    d.graph = a.devices.speedOfSound != b.devices.speedOfSound || !(clockA == b.clock);
    d.osc = !(a.osc == b.osc);
    d.midi = a.programChangeChannel != b.programChangeChannel;
    return d;
}

std::string validate(const SettingsFields& f) {
    if (f.devices.sampleRate <= 0.0) return "Sample rate must be > 0";
    if (f.devices.bufferSize < kMinBufferSize || f.devices.bufferSize > kMaxBufferSize)
        return "Buffer size must be within 32 to 2048";
    if (!(f.devices.speedOfSound >= kMinSpeedOfSound && f.devices.speedOfSound <= kMaxSpeedOfSound))
        return "Speed of sound must be within 300 to 400 m/s";
    if (f.osc.inPort < 1 || f.osc.inPort > 65535) return "OSC input port must be within 1 to 65535";
    if (f.osc.out.port < 1 || f.osc.out.port > 65535) return "OSC output port must be within 1 to 65535";
    if (f.osc.out.host.empty()) return "OSC output host is empty";
    if (f.programChangeChannel < 0 || f.programChangeChannel > 16)
        return "Program-change channel must be within 0 to 16";
    if (!(f.clock.bpm >= kMinBpm && f.clock.bpm <= kMaxBpm)) return "Tempo must be within 20 to 300 bpm";
    if (f.clock.beatsPerBar < 1 || f.clock.beatsPerBar > kMaxBeatsPerBar)
        return "Beats per bar must be within 1 to 32";
    if (!(f.clock.latencyTrimMs >= -1000.0f && f.clock.latencyTrimMs <= 1000.0f))
        return "Clock latency trim must be within -1000 to 1000 ms";
    return {};
}

std::vector<ClockOption> clockOptions(bool linkAvailable) {
    std::vector<ClockOption> out{{ClockKind::Internal, "Internal"},
                                 {ClockKind::Midi, "MIDI clock"},
                                 {ClockKind::Mtc, "MTC"},
                                 {ClockKind::Osc, "OSC"}};
    if (linkAvailable) out.insert(out.begin() + 1, {ClockKind::Link, "Ableton Link"});
    return out;
}

std::string clockKindName(ClockKind kind) {
    switch (kind) {
        case ClockKind::Internal:
            return "Internal";
        case ClockKind::Link:
            return "Ableton Link";
        case ClockKind::Midi:
            return "MIDI clock";
        case ClockKind::Mtc:
            return "MTC";
        case ClockKind::Osc:
            return "OSC";
    }
    return "Internal";
}

std::string clockKindHint(ClockKind kind) {
    switch (kind) {
        case ClockKind::Internal:
            return "Tempo and transport are set here, by OSC or by MIDI mappings.";
        case ClockKind::Link:
            return "Joins the Ableton Link session on the network.";
        case ClockKind::Midi:
            return "Follows MIDI beat clock and start / stop from any enabled MIDI input.";
        case ClockKind::Mtc:
            return "Follows MIDI time code from any enabled MIDI input.";
        case ClockKind::Osc:
            return "Follows /spacer/clock/beat on the OSC input port.";
    }
    return {};
}

std::string clockLockText(ClockKind configured, ClockKind active, bool locked) {
    if (configured == ClockKind::Internal) return "Internal";
    if (active == ClockKind::Internal) return "Unavailable, running internal";
    return locked ? "Locked" : "Searching...";
}

std::vector<AudioDeviceInfo> aggregateCandidates(const std::vector<AudioDeviceInfo>& all) {
    std::vector<AudioDeviceInfo> out;
    for (const auto& d : all)
        if (d.numOutputs > 0 && d.name != "Spacer") out.push_back(d);
    return out;
}

std::string aggregateUid(const std::string& name) {
    std::string tail;
    for (const char c : trim(name)) tail += std::isalnum(static_cast<unsigned char>(c)) ? c : '-';
    return "org.spacer.aggregate." + tail;
}

AggregateRequest planAggregate(const std::string& rawName, const std::vector<std::string>& chosen,
                               const std::vector<AudioDeviceInfo>& all) {
    AggregateRequest r;
    r.name = trim(rawName);
    r.uid = aggregateUid(r.name);
    if (r.name.empty()) {
        r.error = "Enter a name for the aggregate device";
        return r;
    }
    if (std::any_of(all.begin(), all.end(),
                    [&](const AudioDeviceInfo& d) { return d.name == r.name || d.uid == r.uid; })) {
        r.error = "A device named \"" + r.name + "\" already exists";
        return r;
    }
    const auto candidates = aggregateCandidates(all);
    for (const auto& uid : chosen) {
        if (std::find(r.subDeviceUids.begin(), r.subDeviceUids.end(), uid) != r.subDeviceUids.end()) continue;
        if (std::none_of(candidates.begin(), candidates.end(),
                         [&](const AudioDeviceInfo& d) { return d.uid == uid; })) {
            r.error = "Unknown or unsuitable device: " + uid;
            return r;
        }
        r.subDeviceUids.push_back(uid);
    }
    if (r.subDeviceUids.size() < 2) r.error = "Choose at least two devices";
    return r;
}

}  // namespace spacer::settings

namespace spacer::edits {

ProjectEdit setSettings(const Project& project, settings::SettingsFields after, std::string name,
                        std::string mergeKey) {
    ProjectEdit e;
    e.name = std::move(name);
    e.mergeKey = std::move(mergeKey);
    auto before = settings::fieldsOf(project);
    e.effect = settings::classify(before, after).graph ? EngineEffect::structural() : EngineEffect::none();
    e.apply = [after = std::move(after)](Project& p) { settings::apply(p, after); };
    e.revert = [before = std::move(before)](Project& p) { settings::apply(p, before); };
    return e;
}

}  // namespace spacer::edits
