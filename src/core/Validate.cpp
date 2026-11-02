#include "core/Validate.h"

#include <cmath>
#include <set>

namespace spacer {

namespace {

struct Sink {
    Issues& out;
    void error(std::string path, std::string msg) {
        out.push_back({Issue::Severity::Error, std::move(path), std::move(msg)});
    }
    void warn(std::string path, std::string msg) {
        out.push_back({Issue::Severity::Warning, std::move(path), std::move(msg)});
    }
};

std::string idx(const std::string& base, std::size_t i) {
    return base + "[" + std::to_string(i) + "]";
}
std::string sub(const std::string& base, const char* key) {
    return base.empty() ? key : base + "." + key;
}

bool finite(float v) {
    return std::isfinite(v);
}
bool finite(const Position& p) {
    return finite(p.azimuth) && finite(p.elevation) && finite(p.distance);
}
bool finite(const Cartesian& c) {
    return finite(c.x) && finite(c.y) && finite(c.z);
}

void checkPosition(Sink& s, const std::string& path, const Position& p) {
    if (!finite(p)) {
        s.error(path, "position values must be finite");
        return;
    }
    if (p.elevation < -90.0f || p.elevation > 90.0f) s.error(path, "elevation must be within [-90, 90]");
    if (p.distance < 0.0f) s.error(path, "distance must be >= 0");
}

void checkPeriod(Sink& s, const std::string& path, const Period& p) {
    if (!std::isfinite(p.value) || p.value <= 0.0) s.error(path, "period must be > 0");
}

void checkRange(Sink& s, const std::string& path, float v, float lo, float hi, const char* what) {
    if (!finite(v) || v < lo || v > hi)
        s.error(path, std::string(what) + " must be within [" + std::to_string(lo) + ", " +
                          std::to_string(hi) + "]");
}

void checkInserts(Sink& s, const std::string& path, const std::vector<PluginSlot>& inserts) {
    if (static_cast<int>(inserts.size()) > kMaxInserts)
        s.error(path, "at most " + std::to_string(kMaxInserts) + " inserts allowed");
    for (std::size_t i = 0; i < inserts.size(); ++i)
        if (inserts[i].pluginId.empty()) s.error(idx(path, i) + ".pluginId", "pluginId must not be empty");
}

struct MotionChecker {
    Sink& s;
    const std::string& path;
    const Layout& layout;

    void operator()(const ManualMotion& m) const {
        checkPosition(s, sub(path, "position"), m.position);
        if (!finite(m.smoothMs) || m.smoothMs < 0.0f) s.error(sub(path, "smoothMs"), "smoothMs must be >= 0");
    }
    void operator()(const OrbitMotion& m) const {
        checkPosition(s, sub(path, "centre"), m.centre);
        if (!finite(m.radius) || m.radius < 0.0f) s.error(sub(path, "radius"), "radius must be >= 0");
        checkPeriod(s, sub(path, "period"), m.period);
        checkPeriod(s, sub(path, "wobblePeriod"), m.wobblePeriod);
        if (m.direction != 1 && m.direction != -1)
            s.error(sub(path, "direction"), "direction must be 1 or -1");
        if (!finite(m.elevationWobble) || m.elevationWobble < 0.0f)
            s.error(sub(path, "elevationWobble"), "elevationWobble must be >= 0");
    }
    void operator()(const LineMotion& m) const {
        checkPosition(s, sub(path, "from"), m.from);
        checkPosition(s, sub(path, "to"), m.to);
        checkPeriod(s, sub(path, "period"), m.period);
    }
    void operator()(const LissajousMotion& m) const {
        checkPosition(s, sub(path, "centre"), m.centre);
        for (float v : {m.ax, m.ay, m.az, m.fx, m.fy, m.fz})
            if (!finite(v)) s.error(path, "amplitudes and frequencies must be finite");
        checkPeriod(s, sub(path, "period"), m.period);
    }
    void operator()(const PathMotion& m) const {
        if (m.points.size() < 2) s.error(sub(path, "points"), "a path needs at least 2 points");
        for (std::size_t i = 0; i < m.points.size(); ++i)
            if (!finite(m.points[i])) s.error(idx(sub(path, "points"), i), "point values must be finite");
        checkPeriod(s, sub(path, "period"), m.period);
    }
    void operator()(const RandomWalkMotion& m) const {
        if (const auto* box = std::get_if<BoxBounds>(&m.bounds)) {
            if (!finite(box->min) || !finite(box->max))
                s.error(sub(path, "bounds"), "bounds must be finite");
            else if (box->min.x > box->max.x || box->min.y > box->max.y || box->min.z > box->max.z)
                s.error(sub(path, "bounds"), "box min must be <= max on every axis");
        } else {
            const auto& sp = std::get<SphereBounds>(m.bounds);
            if (!finite(sp.centre) || !finite(sp.radius) || sp.radius <= 0.0f)
                s.error(sub(path, "bounds"), "sphere radius must be > 0");
        }
        if (!finite(m.stepMetres) || m.stepMetres < 0.0f)
            s.error(sub(path, "stepMetres"), "stepMetres must be >= 0");
        checkPeriod(s, sub(path, "stepPeriod"), m.stepPeriod);
    }
    void operator()(const StepSequenceMotion& m) const {
        if (m.steps.empty()) s.error(sub(path, "steps"), "a step sequence needs at least one step");
        for (std::size_t i = 0; i < m.steps.size(); ++i) {
            const auto& st = m.steps[i];
            const std::string p = idx(sub(path, "steps"), i);
            if (const auto* id = std::get_if<int>(&st.target)) {
                if (!layout.findSpeaker(*id))
                    s.error(p + ".target", "unknown speaker id " + std::to_string(*id));
            } else {
                checkPosition(s, p + ".target", std::get<Position>(st.target));
            }
            checkPeriod(s, p + ".duration", st.duration);
            if (st.transition == StepTransition::Glide) checkPeriod(s, p + ".glideTime", st.glideTime);
            checkRange(s, p + ".probability", st.probability, 0.0f, 1.0f, "probability");
        }
    }
};

void checkMotion(Sink& s, const std::string& path, const MotionSpec& m, const Layout& layout) {
    std::visit(MotionChecker{s, path, layout}, m.value);
}

void checkModulators(Sink& s, const std::string& path, const std::vector<ModulatorSpec>& mods) {
    if (static_cast<int>(mods.size()) > kMaxModulators)
        s.error(path, "at most " + std::to_string(kMaxModulators) + " modulators allowed");
    for (std::size_t i = 0; i < mods.size(); ++i) {
        const std::string p = idx(path, i);
        if (mods[i].target.empty()) s.error(p + ".target", "target parameter path must not be empty");
        if (!finite(mods[i].depth) || !finite(mods[i].offset)) s.error(p, "depth and offset must be finite");
        if (const auto* lfo = std::get_if<LfoSource>(&mods[i].source))
            checkPeriod(s, p + ".source.period", lfo->period);
        if (const auto* osc = std::get_if<OscSource>(&mods[i].source))
            if (osc->address.empty() || osc->address[0] != '/')
                s.error(p + ".source.address", "OSC address must start with '/'");
        if (const auto* midi = std::get_if<MidiCcSource>(&mods[i].source))
            if (midi->cc < 0 || midi->cc > 127) s.error(p + ".source.cc", "cc must be within [0, 127]");
    }
}

void checkChannelInput(Sink& s, const std::string& path, const ChannelInput& c, int maxChannel, bool stereo) {
    if (c.left < 0 || c.left > maxChannel) s.error(path, "channel out of range");
    if (stereo && !c.isStereo()) s.error(path, "stereo source needs {left, right} channels");
    if (!stereo && c.isStereo()) s.error(path, "mono source must use a single channel");
    if (c.isStereo() && (c.right < 0 || c.right > maxChannel)) s.error(path, "channel out of range");
}

void checkSource(Sink& s, const std::string& path, const SourceSpec& src, const Layout& layout) {
    if (src.id < 1) s.error(sub(path, "id"), "source id must be >= 1");
    const std::string in = sub(path, "input");
    if (const auto* v = std::get_if<VirtualInputSpec>(&src.input.value)) {
        checkChannelInput(s, in, *v, kVirtualDeviceChannels - 1, src.stereo);
    } else if (const auto* h = std::get_if<HardwareInputSpec>(&src.input.value)) {
        checkChannelInput(s, in, *h, 1023, src.stereo);
    } else if (const auto* f = std::get_if<FileInputSpec>(&src.input.value)) {
        if (f->path.empty()) s.error(in + ".path", "file path must not be empty");
        if (!finite(f->gainDb) || !finite(f->startSeconds) || f->startSeconds < 0.0f)
            s.error(in, "gainDb must be finite and startSeconds >= 0");
    } else {
        const auto& g = std::get<GeneratorSpec>(src.input.value);
        if (g.type == GeneratorKind::Sine && (!finite(g.hz) || g.hz <= 0.0f))
            s.error(in + ".hz", "sine frequency must be > 0");
        if (g.type == GeneratorKind::Impulse && (!finite(g.periodSeconds) || g.periodSeconds <= 0.0f))
            s.error(in + ".periodSeconds", "impulse period must be > 0");
    }
    if (!finite(src.gainDb)) s.error(sub(path, "gainDb"), "gain must be finite");
    checkRange(s, sub(path, "spread"), src.spread, 0.0f, 1.0f, "spread");
    if (src.stereo && (!finite(src.width) || src.width <= 0.0f || src.width > 360.0f))
        s.error(sub(path, "width"), "width must be within (0, 360]");
    if (src.linkedPoints && !src.stereo)
        s.error(sub(path, "linkedPoints"), "linkedPoints requires a stereo source");
    if (src.motionRight && !src.linkedPoints)
        s.warn(sub(path, "motionRight"), "motionRight is ignored unless linkedPoints is set");
    if (src.spatializer == SpatializerKind::Direct) {
        if (src.directSpeakers.empty())
            s.error(sub(path, "directSpeakers"), "Direct spatializer needs at least one speaker");
        for (int id : src.directSpeakers)
            if (!layout.findSpeaker(id))
                s.error(sub(path, "directSpeakers"), "unknown speaker id " + std::to_string(id));
    }
    if (!finite(src.distance.k) || src.distance.k < 0.0f) s.error(sub(path, "distance.k"), "k must be >= 0");
    if (!finite(src.distance.rMin) || src.distance.rMin <= 0.0f)
        s.error(sub(path, "distance.rMin"), "rMin must be > 0");
    if (!finite(src.distance.q) || src.distance.q < 0.0f) s.error(sub(path, "distance.q"), "q must be >= 0");
    checkMotion(s, sub(path, "motion"), src.motion, layout);
    if (src.motionRight) checkMotion(s, sub(path, "motionRight"), *src.motionRight, layout);
    checkModulators(s, sub(path, "modulators"), src.modulators);
    checkInserts(s, sub(path, "inserts"), src.inserts);
}

void checkSources(Sink& s, const std::string& path, const std::vector<SourceSpec>& sources,
                  const Layout& layout) {
    if (static_cast<int>(sources.size()) > kMaxSources)
        s.error(path, "at most " + std::to_string(kMaxSources) + " sources allowed");
    std::set<int> ids;
    std::set<std::string> names;
    for (std::size_t i = 0; i < sources.size(); ++i) {
        const std::string p = idx(path, i);
        if (!ids.insert(sources[i].id).second)
            s.error(p + ".id", "duplicate source id " + std::to_string(sources[i].id));
        if (!sources[i].name.empty() && !names.insert(sources[i].name).second)
            s.warn(p + ".name", "duplicate source name '" + sources[i].name + "' (ambiguous for OSC)");
        checkSource(s, p, sources[i], layout);
    }
}

void checkChains(Sink& s, const std::string& path, const std::vector<SpeakerChainSpec>& chains,
                 const Layout& layout) {
    std::set<int> ids;
    for (std::size_t i = 0; i < chains.size(); ++i) {
        const auto& c = chains[i];
        const std::string p = idx(path, i);
        if (!layout.findSpeaker(c.speakerId))
            s.error(p + ".speakerId", "unknown speaker id " + std::to_string(c.speakerId));
        if (!ids.insert(c.speakerId).second)
            s.error(p + ".speakerId", "duplicate speaker chain " + std::to_string(c.speakerId));
        if (!finite(c.trimDb) || !finite(c.userDelayMs) || c.userDelayMs < 0.0f)
            s.error(p, "trimDb must be finite and userDelayMs >= 0");
        for (float f : {c.eq.lowFreq, c.eq.midFreq, c.eq.highFreq})
            if (!finite(f) || f <= 0.0f || f >= 24000.0f)
                s.error(p + ".eq", "EQ frequencies must be within (0, 24000) Hz");
        if (!finite(c.eq.midQ) || c.eq.midQ <= 0.0f) s.error(p + ".eq.midQ", "Q must be > 0");
        checkInserts(s, p + ".inserts", c.inserts);
    }
}

}  // namespace

Issues validate(const Layout& layout) {
    Issues out;
    Sink s{out};
    const auto n = layout.speakers.size();
    if (n < 2 || n > static_cast<std::size_t>(kMaxSpeakers))
        s.error("layout.speakers", "a layout needs 2 to " + std::to_string(kMaxSpeakers) + " speakers (has " +
                                       std::to_string(n) + ")");

    std::set<int> ids, channels;
    for (std::size_t i = 0; i < n; ++i) {
        const auto& sp = layout.speakers[i];
        const std::string p = idx("layout.speakers", i);
        if (sp.id < 1) s.error(p + ".id", "speaker id must be >= 1");
        if (!ids.insert(sp.id).second) s.error(p + ".id", "duplicate speaker id " + std::to_string(sp.id));
        if (sp.outputChannel < -1)
            s.error(p + ".outputChannel", "outputChannel must be >= 0, or -1 for unassigned");
        else if (sp.outputChannel >= 0 && !channels.insert(sp.outputChannel).second)
            s.error(p + ".outputChannel", "duplicate output channel " + std::to_string(sp.outputChannel));
        checkPosition(s, p + ".position", sp.position);
        if (finite(sp.position.distance) && sp.position.distance <= 0.0f)
            s.error(p + ".position.distance", "speaker distance must be > 0");
        if (!finite(sp.trimDb)) s.error(p + ".trimDb", "trim must be finite");
        if (!finite(sp.userDelayMs) || sp.userDelayMs < 0.0f)
            s.error(p + ".userDelayMs", "userDelayMs must be >= 0");
    }
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t k = i + 1; k < n; ++k) {
            const auto& a = layout.speakers[i].position;
            const auto& b = layout.speakers[k].position;
            if (finite(a) && finite(b) && angleBetween(a, b) < 0.5f)
                s.warn(idx("layout.speakers", k),
                       "speaker has the same direction as speaker index " + std::to_string(i));
        }
    return out;
}

Issues validate(const SourceSpec& source, const Layout& layout, const std::string& pathPrefix) {
    Issues out;
    Sink s{out};
    checkSource(s, pathPrefix, source, layout);
    return out;
}

Issues validate(const Project& p) {
    Issues out = validate(p.layout);
    Sink s{out};

    if (p.schemaVersion != kProjectSchemaVersion) s.error("schemaVersion", "unsupported schemaVersion");
    if (p.devices.sampleRate <= 0.0) s.error("devices.sampleRate", "sampleRate must be > 0");
    if (p.devices.bufferSize < 32 || p.devices.bufferSize > 2048)
        s.error("devices.bufferSize", "bufferSize must be within [32, 2048]");
    if (!finite(p.devices.speedOfSound) || p.devices.speedOfSound <= 0.0f)
        s.error("devices.speedOfSound", "speed of sound must be > 0");
    if (p.ambisonics.order < 1 || p.ambisonics.order > 5)
        s.error("ambisonics.order", "order must be within [1, 5]");
    if (!finite(p.master.gainDb)) s.error("master.gainDb", "gain must be finite");

    checkSources(s, "sources", p.sources, p.layout);
    checkChains(s, "speakerChains", p.speakerChains, p.layout);

    std::set<std::string> sceneNames;
    for (std::size_t i = 0; i < p.scenes.size(); ++i) {
        const auto& sc = p.scenes[i];
        const std::string path = idx("scenes", i);
        if (sc.name.empty())
            s.error(path + ".name", "scene name must not be empty");
        else if (!sceneNames.insert(sc.name).second)
            s.error(path + ".name", "duplicate scene name '" + sc.name + "'");
        checkSources(s, path + ".sources", sc.sources, p.layout);
        checkChains(s, path + ".speakerChains", sc.speakerChains, p.layout);
        if (!finite(sc.master.gainDb)) s.error(path + ".master.gainDb", "gain must be finite");
    }

    std::set<std::string> listNames;
    for (std::size_t i = 0; i < p.cueLists.size(); ++i) {
        const auto& cl = p.cueLists[i];
        const std::string path = idx("cueLists", i);
        if (cl.name.empty())
            s.error(path + ".name", "cue list name must not be empty");
        else if (!listNames.insert(cl.name).second)
            s.error(path + ".name", "duplicate cue list name '" + cl.name + "'");
        for (std::size_t k = 0; k < cl.cues.size(); ++k) {
            const auto& c = cl.cues[k];
            const std::string cp = idx(path + ".cues", k);
            if (sceneNames.find(c.sceneName) == sceneNames.end())
                s.error(cp + ".scene", "cue references unknown scene '" + c.sceneName + "'");
            if (!finite(c.transitionSeconds) || c.transitionSeconds < 0.0f)
                s.error(cp + ".transitionSeconds", "transition must be >= 0");
            if (!finite(c.postWaitSeconds) || c.postWaitSeconds < 0.0f)
                s.error(cp + ".postWaitSeconds", "postWait must be >= 0");
            const std::string tp = cp + ".trigger";
            if (const auto* atBeat = std::get_if<trigger::AtBeat>(&c.trigger.value)) {
                if (atBeat->bar < 1) s.error(tp + ".bar", "bar must be >= 1");
                if (!std::isfinite(atBeat->beat) || atBeat->beat < 1.0 ||
                    atBeat->beat >= p.clock.beatsPerBar + 1)
                    s.error(tp + ".beat", "beat must be within [1, beatsPerBar + 1)");
            } else if (const auto* after = std::get_if<trigger::AfterPrevious>(&c.trigger.value)) {
                if (!std::isfinite(after->delay.value) || after->delay.value < 0.0)
                    s.error(tp + ".delay", "delay must be >= 0");
                if (!std::isfinite(after->randomRange) || after->randomRange < 0.0)
                    s.error(tp + ".randomRange", "randomRange must be >= 0");
            } else if (const auto* osc = std::get_if<trigger::Osc>(&c.trigger.value)) {
                if (osc->address.empty() || osc->address[0] != '/')
                    s.error(tp + ".address", "OSC address must start with '/'");
            } else if (const auto* midi = std::get_if<trigger::Midi>(&c.trigger.value)) {
                if (midi->number < 0 || midi->number > 127)
                    s.error(tp + ".number", "number must be within [0, 127]");
                if (midi->channel < 0 || midi->channel > 16)
                    s.error(tp + ".channel", "channel must be within [0, 16]");
            } else if (const auto* tod = std::get_if<trigger::TimeOfDay>(&c.trigger.value)) {
                if (tod->hour < 0 || tod->hour > 23 || tod->minute < 0 || tod->minute > 59)
                    s.error(tp, "time must be 00:00..23:59");
                for (int d : tod->days)
                    if (d < 0 || d > 6) s.error(tp + ".days", "days must be within [0, 6]");
            }
        }
    }

    if (p.osc.inPort < 1 || p.osc.inPort > 65535) s.error("osc.inPort", "port must be within [1, 65535]");
    if (p.osc.out.port < 1 || p.osc.out.port > 65535)
        s.error("osc.out.port", "port must be within [1, 65535]");
    for (std::size_t i = 0; i < p.midi.mappings.size(); ++i) {
        const auto& m = p.midi.mappings[i];
        const std::string path = idx("midi.mappings", i);
        if (m.parameterPath.empty()) s.error(path + ".parameterPath", "parameterPath must not be empty");
        if (m.channel < 0 || m.channel > 16) s.error(path + ".channel", "channel must be within [0, 16]");
        if (m.number < 0 || m.number > (m.type == MidiMessageType::Nrpn ? 16383 : 127))
            s.error(path + ".number", "number out of range");
        if (!finite(m.min) || !finite(m.max)) s.error(path, "min and max must be finite");
        if (m.curve == MidiCurve::Log && finite(m.min) && finite(m.max) && (m.min <= 0.0f || m.max <= 0.0f))
            s.error(path, "log curve needs min and max > 0");
    }
    if (!std::isfinite(p.clock.bpm) || p.clock.bpm < 20.0 || p.clock.bpm > 300.0)
        s.error("clock.bpm", "bpm must be within [20, 300]");
    if (p.clock.beatsPerBar < 1) s.error("clock.beatsPerBar", "beatsPerBar must be >= 1");
    return out;
}

}  // namespace spacer
