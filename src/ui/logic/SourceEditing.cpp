#include "ui/logic/SourceEditing.h"

#include <algorithm>
#include <cmath>
#include <set>

#include "core/motion/MotionParamValue.h"
#include "core/motion/ParameterPath.h"
#include "ui/logic/FieldBuilder.h"
#include "ui/logic/SourceGeometry.h"

namespace spacer::sourceedit {

using namespace fields;

namespace {

constexpr int kMaxVirtualChannel = kVirtualDeviceChannels - 1;
constexpr int kMaxHardwareChannel = 1023;

const SourceSpec* findSource(const Project& p, int id) {
    for (const auto& s : p.sources)
        if (s.id == id) return &s;
    return nullptr;
}

ChannelInput* channelsOf(SourceSpec& s) {
    if (auto* v = std::get_if<VirtualInputSpec>(&s.input.value)) return v;
    if (auto* h = std::get_if<HardwareInputSpec>(&s.input.value)) return h;
    return nullptr;
}

int maxChannelOf(const SourceSpec& s) {
    return std::holds_alternative<HardwareInputSpec>(s.input.value) ? kMaxHardwareChannel
                                                                    : kMaxVirtualChannel;
}

int lowestUnusedVirtualChannel(const Project& project) {
    std::set<int> used;
    for (const auto& s : project.sources)
        if (const auto* v = std::get_if<VirtualInputSpec>(&s.input.value)) {
            used.insert(v->left);
            if (v->right >= 0) used.insert(v->right);
        }
    for (int ch = 0; ch <= kMaxVirtualChannel; ++ch)
        if (!used.count(ch)) return ch;
    return 0;
}

/// A 1-based channel number box over a 0-based model channel.
void channelBox(Builder& b, const std::string& id, const std::string& label, int& ref, int maxChannel) {
    float shadow = static_cast<float>(ref + 1);
    b.number(id, label, Unit::Count, 1.0f, static_cast<float>(maxChannel + 1), shadow);
    b.lastSetNumber([&ref, maxChannel](float v) {
        if (!std::isfinite(v)) return false;
        ref = std::clamp(static_cast<int>(std::lround(v)), 1, maxChannel + 1) - 1;
        return true;
    });
}

void bindInput(Builder& b, SourceSpec& s) {
    b.group("Input");
    b.choice("input.kind", "Type", {"Virtual input", "Hardware input", "File", "Generator"},
             static_cast<int>(s.kind()), [&s](int i) { return setInputKind(s, static_cast<SourceKind>(i)); });
    if (s.kind() != SourceKind::Generator) {
        bool stereo = s.stereo;  // written through setStereo(): it also repairs channels / linking
        b.toggle("stereo", "Stereo", stereo);
        b.lastSetToggle([&s](bool v) { return setStereo(s, v); });
    }
    if (auto* ch = channelsOf(s)) {
        const int maxCh = maxChannelOf(s);
        channelBox(b, "input.left", s.stereo ? "Left channel" : "Channel", ch->left, maxCh);
        if (s.stereo) channelBox(b, "input.right", "Right channel", ch->right, maxCh);
    } else if (auto* f = std::get_if<FileInputSpec>(&s.input.value)) {
        b.text("input.path", "File", f->path);
        b.toggle("input.loop", "Loop", f->loop);
        b.number("input.gainDb", "File gain", Unit::Db, kGainMinDb, kGainMaxDb, f->gainDb);
        b.number("input.startSeconds", "Start at", Unit::Seconds, 0.0f, 86400.0f, f->startSeconds);
        b.toggle("input.playOnSceneRecall", "Play on scene recall", f->playOnSceneRecall);
    } else if (auto* g = std::get_if<GeneratorSpec>(&s.input.value)) {
        b.enumChoice("input.generator", "Generator", {"Sine", "Pink noise", "White noise", "Impulse"},
                     g->type);
        if (g->type == GeneratorKind::Sine)
            b.number("input.hz", "Frequency", Unit::Hertz, 1.0f, 20000.0f, g->hz);
        if (g->type == GeneratorKind::Impulse)
            b.number("input.periodSeconds", "Impulse period", Unit::Seconds, 0.01f, 60.0f, g->periodSeconds);
    }
}

void bindLevel(Builder& b, SourceSpec& s) {
    b.group("Level");
    b.text("name", "Name", s.name);
    b.toggle("mute", "Mute", s.mute);
    b.number("gainDb", "Gain", Unit::Db, kGainMinDb, kGainMaxDb, s.gainDb);
}

void bindSpatial(Builder& b, SourceSpec& s, const Layout& layout) {
    b.group("Spatializer");
    b.choice("spatializer", "Algorithm", {"VBAP", "DBAP", "Ambisonic", "Direct"},
             static_cast<int>(s.spatializer),
             [&s, &layout](int i) { return setSpatializer(s, static_cast<SpatializerKind>(i), layout); });
    if (s.spatializer == SpatializerKind::Direct) {
        b.group("Direct speakers");
        for (const auto& sp : layout.speakers) {
            const bool on =
                std::find(s.directSpeakers.begin(), s.directSpeakers.end(), sp.id) != s.directSpeakers.end();
            bool shadow = on;
            b.toggle("direct." + std::to_string(sp.id), sp.name.empty() ? std::to_string(sp.id) : sp.name,
                     shadow);
            b.lastSetToggle([&s, id = sp.id](bool v) { return setDirectSpeaker(s, id, v); });
        }
        return;  // Direct ignores spread, position and the distance model
    }
    b.number("spread", "Spread", Unit::Percent, 0.0f, 1.0f, s.spread);
    if (s.stereo) {
        b.number("width", "Stereo width", Unit::Degrees, 1.0f, 360.0f, s.width);
        bool linked = s.linkedPoints;
        b.toggle("linkedPoints", "Link points", linked);
        b.lastSetToggle([&s](bool v) { return setLinkedPoints(s, v); });
    }
    b.group("Distance");
    b.number("distance.k", "Rolloff k", Unit::None, 0.0f, 4.0f, s.distance.k);
    b.number("distance.rMin", "Min radius", Unit::Metres, 0.1f, 10.0f, s.distance.rMin);
    b.number("distance.q", "Low-pass q", Unit::None, 0.0f, 4.0f, s.distance.q);
}

void bindModulators(Builder& b, SourceSpec& s) {
    const auto targets = modulatorTargets(s);
    for (std::size_t i = 0; i < s.modulators.size(); ++i) {
        auto& m = s.modulators[i];
        const int item = static_cast<int>(i);
        b.group("Modulator " + std::to_string(i + 1), item);
        b.setPrefix("mod." + std::to_string(i) + ".");
        b.choice("source", "Source", {"LFO", "OSC", "MIDI CC"}, static_cast<int>(m.source.index()),
                 [&m](int k) {
                     if (k == 0 && !std::holds_alternative<LfoSource>(m.source))
                         m.source = LfoSource{};
                     else if (k == 1 && !std::holds_alternative<OscSource>(m.source))
                         m.source = OscSource{"/mod/1"};
                     else if (k == 2 && !std::holds_alternative<MidiCcSource>(m.source))
                         m.source = MidiCcSource{1};
                     return true;
                 });
        if (auto* lfo = std::get_if<LfoSource>(&m.source)) {
            b.enumChoice("shape", "Shape", {"Sine", "Triangle", "Saw", "Square", "Sample & hold"},
                         lfo->shape);
            b.period("period", "Period", lfo->period);
        } else if (auto* osc = std::get_if<OscSource>(&m.source)) {
            b.text("address", "OSC address", osc->address);
            b.lastSetText([osc](const std::string& t) {
                if (t.empty() || t.front() != '/') return false;  // SPEC §10 / validate()
                osc->address = t;
                return true;
            });
        } else if (auto* cc = std::get_if<MidiCcSource>(&m.source)) {
            b.integer("cc", "CC number", 0, 127, cc->cc);
        }
        std::vector<std::string> choices = targets;
        auto it = std::find(choices.begin(), choices.end(), m.target);
        if (it == choices.end()) {
            choices.push_back(m.target.empty() ? "(none)" : m.target);
            it = choices.end() - 1;
        }
        const auto current = static_cast<int>(it - choices.begin());
        b.choice("target", "Target", choices, current, [&m, choices](int k) {
            const auto& t = choices[static_cast<std::size_t>(k)];
            if (!ParameterPath::parse(t)) return false;  // the "(none)" / stale entry
            m.target = t;
            return true;
        });
        b.number("depth", "Depth", Unit::None, -1000.0f, 1000.0f, m.depth);
        b.number("offset", "Offset", Unit::None, -1000.0f, 1000.0f, m.offset);
        b.setPrefix("");
    }
}

std::vector<Binding> bindSource(SourceSpec& s, const Layout& layout, std::optional<SourceSection> only) {
    Builder b(&layout);
    const auto wants = [&only](SourceSection sec) { return !only || *only == sec; };
    if (wants(SourceSection::Input)) bindInput(b, s);
    if (wants(SourceSection::Level)) bindLevel(b, s);
    if (wants(SourceSection::Spatial)) bindSpatial(b, s, layout);
    if (wants(SourceSection::Modulators)) bindModulators(b, s);
    return b.take();
}

}  // namespace

// ---- model operations ---------------------------------------------------------

int nextSourceId(const Project& project) {
    int id = 0;
    for (const auto& s : project.sources) id = std::max(id, s.id);
    return id + 1;
}

std::string uniqueName(const Project& project, const std::string& base) {
    const auto taken = [&project](const std::string& n) {
        return std::any_of(project.sources.begin(), project.sources.end(),
                           [&n](const SourceSpec& s) { return s.name == n; });
    };
    if (!taken(base)) return base;
    for (int i = 2;; ++i) {
        const std::string candidate = base + " " + std::to_string(i);
        if (!taken(candidate)) return candidate;
    }
}

const char* sourceKindName(SourceKind kind) {
    switch (kind) {
        case SourceKind::VirtualInput:
            return "Virtual input";
        case SourceKind::HardwareInput:
            return "Hardware input";
        case SourceKind::File:
            return "File";
        case SourceKind::Generator:
            return "Generator";
    }
    return "";
}

const char* spatializerName(SpatializerKind kind) {
    switch (kind) {
        case SpatializerKind::Vbap:
            return "VBAP";
        case SpatializerKind::Dbap:
            return "DBAP";
        case SpatializerKind::Ambisonic:
            return "Ambisonic";
        case SpatializerKind::Direct:
            return "Direct";
    }
    return "";
}

SourceSpec makeSource(const Project& project, SourceKind kind) {
    SourceSpec s;
    s.id = nextSourceId(project);
    s.name = uniqueName(project, "Source " + std::to_string(s.id));
    switch (kind) {
        case SourceKind::VirtualInput: {
            VirtualInputSpec v;
            v.left = lowestUnusedVirtualChannel(project);
            s.input.value = v;
            break;
        }
        case SourceKind::HardwareInput:
            s.input.value = HardwareInputSpec{};
            break;
        case SourceKind::File:
            s.input.value = FileInputSpec{};
            break;
        case SourceKind::Generator:
            s.input.value = GeneratorSpec{};
            break;
    }
    s.motion = makeMotion(MotionKind::Manual, Position{0.0f, 0.0f, 2.0f});
    return s;
}

bool setInputKind(SourceSpec& s, SourceKind kind) {
    if (s.kind() == kind) return true;
    const ChannelInput* old = channelsOf(s);
    const int left = old != nullptr ? old->left : 0;
    switch (kind) {
        case SourceKind::VirtualInput: {
            VirtualInputSpec v;
            v.left = std::min(left, kMaxVirtualChannel);
            s.input.value = v;
            break;
        }
        case SourceKind::HardwareInput: {
            HardwareInputSpec h;
            h.left = left;
            s.input.value = h;
            break;
        }
        case SourceKind::File:
            s.input.value = FileInputSpec{};
            break;
        case SourceKind::Generator:
            s.input.value = GeneratorSpec{};
            s.stereo = false;  // generators are mono (SPEC §8)
            s.linkedPoints = false;
            s.motionRight.reset();
            break;
    }
    if (auto* ch = channelsOf(s)) ch->right = s.stereo ? std::min(ch->left + 1, maxChannelOf(s)) : -1;
    return true;
}

bool setStereo(SourceSpec& s, bool stereo) {
    if (s.stereo == stereo) return true;
    if (stereo && s.kind() == SourceKind::Generator) return false;
    s.stereo = stereo;
    if (auto* ch = channelsOf(s)) ch->right = stereo ? std::min(ch->left + 1, maxChannelOf(s)) : -1;
    if (!stereo) {
        s.linkedPoints = false;
        s.motionRight.reset();
    }
    return true;
}

bool setLinkedPoints(SourceSpec& s, bool linked) {
    if (s.linkedPoints == linked) return true;
    if (linked && !s.stereo) return false;
    s.linkedPoints = linked;
    if (linked)
        s.motionRight = s.motion;
    else
        s.motionRight.reset();
    return true;
}

bool setSpatializer(SourceSpec& s, SpatializerKind kind, const Layout& layout) {
    s.spatializer = kind;
    if (kind == SpatializerKind::Direct && s.directSpeakers.empty() && !layout.speakers.empty())
        s.directSpeakers.push_back(layout.speakers.front().id);
    return true;
}

bool setDirectSpeaker(SourceSpec& s, int speakerId, bool on) {
    auto it = std::find(s.directSpeakers.begin(), s.directSpeakers.end(), speakerId);
    if (on) {
        if (it == s.directSpeakers.end()) s.directSpeakers.push_back(speakerId);
        return true;
    }
    if (it == s.directSpeakers.end()) return true;
    if (s.directSpeakers.size() <= 1) return false;  // validate(): Direct needs a speaker
    s.directSpeakers.erase(it);
    return true;
}

int motionChannelCount(const SourceSpec& s) {
    return s.stereo && s.linkedPoints ? 2 : 1;
}

const MotionSpec& motionOf(const SourceSpec& s, int channel) {
    return motionForChannel(s, channel);
}

MotionSpec& mutableMotionOf(SourceSpec& s, int channel) {
    if (channel == 1 && s.stereo && s.linkedPoints) {
        if (!s.motionRight) s.motionRight = s.motion;
        return *s.motionRight;
    }
    return s.motion;
}

bool addModulator(SourceSpec& s) {
    if (static_cast<int>(s.modulators.size()) >= kMaxModulators) return false;
    ModulatorSpec m;
    m.target = "gain";
    s.modulators.push_back(m);
    return true;
}

bool removeModulator(SourceSpec& s, int index) {
    if (index < 0 || index >= static_cast<int>(s.modulators.size())) return false;
    s.modulators.erase(s.modulators.begin() + index);
    return true;
}

std::vector<std::string> modulatorTargets(const SourceSpec& s) {
    std::vector<std::string> out;
    const auto add = [&out](const std::string& p) {
        if (std::find(out.begin(), out.end(), p) == out.end()) out.push_back(p);
    };
    const auto src = [&add](SourceParam p) {
        ParameterPath path;
        path.domain = ParameterPath::Domain::Source;
        path.source = p;
        add(std::string(path.toString()));
    };
    src(SourceParam::GainDb);
    if (s.spatializer != SpatializerKind::Direct) {
        src(SourceParam::Spread);
        if (s.stereo) src(SourceParam::Width);
        src(SourceParam::DistanceK);
        src(SourceParam::DistanceRMin);
        src(SourceParam::DistanceQ);
    }
    for (int ch = 0; ch < motionChannelCount(s); ++ch)
        for (std::size_t i = 0; i < kNumMotionParams; ++i) {
            const auto p = static_cast<MotionParam>(i);
            if (!motionParamValue(motionOf(s, ch), p)) continue;
            ParameterPath path;
            path.domain = ParameterPath::Domain::Motion;
            path.motion = p;
            add(std::string(path.toString()));
        }
    return out;
}

std::vector<std::string> insertSummaries(const SourceSpec& s) {
    std::vector<std::string> out;
    for (const auto& slot : s.inserts) out.push_back(slot.pluginId.empty() ? "(empty)" : slot.pluginId);
    return out;
}

// ---- field tables ---------------------------------------------------------------

std::vector<MotionField> sourceFields(const SourceSpec& source, const Layout& layout, SourceSection section) {
    SourceSpec copy = source;
    auto bindings = bindSource(copy, layout, section);
    std::vector<MotionField> out;
    out.reserve(bindings.size());
    for (auto& b : bindings) out.push_back(std::move(b.field));
    return out;
}

bool setSourceField(SourceSpec& source, const Layout& layout, const std::string& fieldId,
                    const std::string& text) {
    SourceSpec work = source;
    auto bindings = bindSource(work, layout, std::nullopt);
    for (auto& b : bindings) {
        if (b.field.id != fieldId) continue;
        const bool ok = applyText(b, text);
        if (ok) source = work;
        return ok;
    }
    return false;
}

// ---- edits ----------------------------------------------------------------------

namespace {

/// Copies source `sourceId`, lets `mutate` change it and returns the setSource
/// edit; nullopt if the source is missing, `mutate` refused or nothing changed.
template <typename Fn>
std::optional<ProjectEdit> editSource(const Project& project, int sourceId, Fn&& mutate,
                                      const std::string& name, const std::string& mergeKey) {
    const SourceSpec* before = findSource(project, sourceId);
    if (before == nullptr) return std::nullopt;
    SourceSpec after = *before;
    if (!mutate(after) || after == *before) return std::nullopt;
    auto edit = edits::setSource(project, std::move(after), name);
    if (!edits::isValid(edit)) return std::nullopt;
    edit.mergeKey = mergeKey;
    return edit;
}

std::string labelOf(const std::vector<MotionField>& fields, const std::string& id) {
    for (const auto& f : fields)
        if (f.id == id) return f.label;
    return id;
}

}  // namespace

ProjectEdit addSource(const Project& project, SourceKind kind, int* newId) {
    ProjectEdit e;
    if (static_cast<int>(project.sources.size()) >= kMaxSources) return e;
    SourceSpec spec = makeSource(project, kind);
    if (newId != nullptr) *newId = spec.id;
    e.name = "Add Source";
    e.effect = EngineEffect::structural();
    e.apply = [spec](Project& p) { p.sources.push_back(spec); };
    e.revert = [id = spec.id](Project& p) {
        p.sources.erase(std::remove_if(p.sources.begin(), p.sources.end(),
                                       [id](const SourceSpec& s) { return s.id == id; }),
                        p.sources.end());
    };
    return e;
}

ProjectEdit duplicateSource(const Project& project, int sourceId, int* newId) {
    ProjectEdit e;
    const SourceSpec* original = findSource(project, sourceId);
    if (original == nullptr || static_cast<int>(project.sources.size()) >= kMaxSources) return e;
    SourceSpec copy = *original;
    copy.id = nextSourceId(project);
    copy.name = uniqueName(project, original->name + " copy");
    if (newId != nullptr) *newId = copy.id;
    const auto index = static_cast<std::size_t>(original - project.sources.data()) + 1;
    e.name = "Duplicate " + original->name;
    e.effect = EngineEffect::structural();
    e.apply = [copy, index](Project& p) {
        p.sources.insert(p.sources.begin() + static_cast<std::ptrdiff_t>(std::min(index, p.sources.size())),
                         copy);
    };
    e.revert = [id = copy.id](Project& p) {
        p.sources.erase(std::remove_if(p.sources.begin(), p.sources.end(),
                                       [id](const SourceSpec& s) { return s.id == id; }),
                        p.sources.end());
    };
    return e;
}

ProjectEdit removeSource(const Project& project, int sourceId) {
    ProjectEdit e;
    const SourceSpec* original = findSource(project, sourceId);
    if (original == nullptr) return e;
    const auto index = static_cast<std::size_t>(original - project.sources.data());
    e.name = "Remove " + original->name;
    e.effect = EngineEffect::structural();
    e.apply = [sourceId](Project& p) {
        p.sources.erase(std::remove_if(p.sources.begin(), p.sources.end(),
                                       [sourceId](const SourceSpec& s) { return s.id == sourceId; }),
                        p.sources.end());
    };
    e.revert = [spec = *original, index](Project& p) {
        p.sources.insert(p.sources.begin() + static_cast<std::ptrdiff_t>(std::min(index, p.sources.size())),
                         spec);
    };
    return e;
}

std::optional<ProjectEdit> editSourceField(const Project& project, int sourceId, const std::string& fieldId,
                                           const std::string& text) {
    const SourceSpec* before = findSource(project, sourceId);
    if (before == nullptr) return std::nullopt;
    std::string name = fieldId;
    for (auto section :
         {SourceSection::Input, SourceSection::Level, SourceSection::Spatial, SourceSection::Modulators}) {
        const auto label = labelOf(sourceFields(*before, project.layout, section), fieldId);
        if (label != fieldId) {
            name = label;
            break;
        }
    }
    return editSource(
        project, sourceId, [&](SourceSpec& s) { return setSourceField(s, project.layout, fieldId, text); },
        "Set " + name, "src." + std::to_string(sourceId) + "." + fieldId);
}

std::optional<ProjectEdit> editMotionField(const Project& project, int sourceId, int channel,
                                           const std::string& fieldId, const std::string& text) {
    const SourceSpec* before = findSource(project, sourceId);
    if (before == nullptr || channel < 0 || channel >= motionChannelCount(*before)) return std::nullopt;
    const auto live = liveParamOf(motionOf(*before, channel), fieldId);
    const std::string name = "Set " + labelOf(motionFields(motionOf(*before, channel)), fieldId);
    auto edit = editSource(
        project, sourceId,
        [&](SourceSpec& s) {
            return setMotionField(mutableMotionOf(s, channel), project.layout, fieldId, text);
        },
        name, "motion." + std::to_string(sourceId) + "." + std::to_string(channel) + "." + fieldId);
    if (edit && live) {
        // Only this parameter changed: no rebuild, no phase restart.
        edit->effect =
            EngineEffect::motionParam(sourceId, motionChannelCount(*before) == 2 ? channel : -1, *live);
    }
    return edit;
}

std::optional<ProjectEdit> editMotionKind(const Project& project, int sourceId, int channel,
                                          MotionKind kind) {
    const SourceSpec* before = findSource(project, sourceId);
    if (before == nullptr || channel < 0 || channel >= motionChannelCount(*before)) return std::nullopt;
    if (motionKindOf(motionOf(*before, channel)) == kind) return std::nullopt;
    // Start where the source is now so the switch does not throw it somewhere else.
    Position start{0.0f, 0.0f, 2.0f};
    const auto points = modelPoints(*before, project.layout);
    if (!points.empty()) {
        start = points[std::min(static_cast<std::size_t>(channel), points.size() - 1)];
        if (before->stereo && !before->linkedPoints && points.size() >= 2)
            start.azimuth = 0.5f * (points[0].azimuth + points[1].azimuth);
    }
    return editSource(
        project, sourceId,
        [&](SourceSpec& s) {
            mutableMotionOf(s, channel) = makeMotion(kind, start);
            return true;
        },
        std::string("Set Motion ") + motionKindName(kind), "");
}

std::optional<ProjectEdit> editMotionAddItem(const Project& project, int sourceId, int channel) {
    const SourceSpec* before = findSource(project, sourceId);
    if (before == nullptr || channel < 0 || channel >= motionChannelCount(*before)) return std::nullopt;
    const auto item = motionItemName(motionOf(*before, channel));
    return editSource(
        project, sourceId, [&](SourceSpec& s) { return addMotionItem(mutableMotionOf(s, channel)); },
        "Add " + item, "");
}

std::optional<ProjectEdit> editMotionRemoveItem(const Project& project, int sourceId, int channel,
                                                int index) {
    const SourceSpec* before = findSource(project, sourceId);
    if (before == nullptr || channel < 0 || channel >= motionChannelCount(*before)) return std::nullopt;
    const auto item = motionItemName(motionOf(*before, channel));
    return editSource(
        project, sourceId,
        [&](SourceSpec& s) { return removeMotionItem(mutableMotionOf(s, channel), index); }, "Remove " + item,
        "");
}

std::optional<ProjectEdit> editAddModulator(const Project& project, int sourceId) {
    return editSource(project, sourceId, [](SourceSpec& s) { return addModulator(s); }, "Add Modulator", "");
}

std::optional<ProjectEdit> editRemoveModulator(const Project& project, int sourceId, int index) {
    return editSource(
        project, sourceId, [&](SourceSpec& s) { return removeModulator(s, index); }, "Remove Modulator", "");
}

}  // namespace spacer::sourceedit
