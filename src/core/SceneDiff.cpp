#include "core/SceneDiff.h"

#include <algorithm>
#include <utility>

namespace spacer {

namespace {

const SourceSpec* findSource(const Scene& scene, int id) {
    for (const auto& s : scene.sources)
        if (s.id == id) return &s;
    return nullptr;
}

const SpeakerChainSpec* findChain(const Scene& scene, int speakerId) {
    for (const auto& c : scene.speakerChains)
        if (c.speakerId == speakerId) return &c;
    return nullptr;
}

ManualMotion* asManual(MotionSpec& m) {
    return std::get_if<ManualMotion>(&m.value);
}
const ManualMotion* asManual(const MotionSpec& m) {
    return std::get_if<ManualMotion>(&m.value);
}

/// `base` with its continuous values replaced by those of `from`, where the two
/// are comparable (Manual positions only when both sides are Manual).
SourceSpec withContinuousFrom(SourceSpec base, const SourceSpec& from) {
    base.gainDb = from.gainDb;
    base.mute = from.mute;
    base.spread = from.spread;
    base.width = from.width;
    base.distance = from.distance;
    if (sameInsertStructure(base.inserts, from.inserts)) base.inserts = from.inserts;
    if (auto* m = asManual(base.motion))
        if (const auto* f = asManual(from.motion)) m->position = f->position;
    if (base.motionRight && from.motionRight)
        if (auto* m = asManual(*base.motionRight))
            if (const auto* f = asManual(*from.motionRight)) m->position = f->position;
    return base;
}

/// True when only continuous values (and the name, and the inserts, which
/// are diffed separately) differ.
bool sameStructure(const SourceSpec& a, const SourceSpec& b) {
    SourceSpec probe = withContinuousFrom(b, a);
    probe.name = a.name;
    probe.inserts = a.inserts;
    return probe == a;
}

void addScalar(std::vector<Ramp>& out, RampTarget t, int id, float from, float to) {
    if (from == to) return;
    Ramp r;
    r.target = t;
    r.id = id;
    r.from = from;
    r.to = to;
    out.push_back(r);
}

void addPosition(std::vector<Ramp>& out, int id, int channel, const ManualMotion* from,
                 const ManualMotion* to) {
    if (from == nullptr || to == nullptr || from->position == to->position) return;
    Ramp r;
    r.target = RampTarget::SourcePosition;
    r.id = id;
    r.channel = channel;
    r.fromPosition = from->position;
    r.toPosition = to->position;
    out.push_back(r);
}

void diffSource(std::vector<Ramp>& out, const SourceSpec& a, const SourceSpec& b) {
    addScalar(out, RampTarget::SourceGainDb, b.id, a.gainDb, b.gainDb);
    addScalar(out, RampTarget::SourceMute, b.id, a.mute ? 1.0f : 0.0f, b.mute ? 1.0f : 0.0f);
    addScalar(out, RampTarget::SourceSpread, b.id, a.spread, b.spread);
    addScalar(out, RampTarget::SourceWidth, b.id, a.width, b.width);
    addScalar(out, RampTarget::SourceDistanceK, b.id, a.distance.k, b.distance.k);
    addScalar(out, RampTarget::SourceDistanceRMin, b.id, a.distance.rMin, b.distance.rMin);
    addScalar(out, RampTarget::SourceDistanceQ, b.id, a.distance.q, b.distance.q);
    const bool linked = b.stereo && b.linkedPoints;
    addPosition(out, b.id, linked ? 0 : -1, asManual(a.motion), asManual(b.motion));
    if (linked && a.motionRight && b.motionRight)
        addPosition(out, b.id, 1, asManual(*a.motionRight), asManual(*b.motionRight));
}

/// Parameter ramps and bypass switches between two inserts lists of the same
/// structure. Parameters only the current side has are left alone.
void diffInserts(std::vector<Ramp>& out, bool speaker, int id, const std::vector<PluginSlot>& a,
                 const std::vector<PluginSlot>& b) {
    for (std::size_t i = 0; i < b.size() && i < a.size(); ++i) {
        const int slot = static_cast<int>(i);
        if (a[i].bypassed != b[i].bypassed) {
            Ramp r;
            r.target = speaker ? RampTarget::SpeakerInsertBypass : RampTarget::SourceInsertBypass;
            r.id = id;
            r.slot = slot;
            r.from = a[i].bypassed ? 1.0f : 0.0f;
            r.to = b[i].bypassed ? 1.0f : 0.0f;
            out.push_back(r);
        }
        for (const auto& [index, value] : b[i].params) {
            const auto it = a[i].params.find(index);
            if (it != a[i].params.end() && it->second == value) continue;
            Ramp r;
            r.target = speaker ? RampTarget::SpeakerInsertParam : RampTarget::SourceInsertParam;
            r.id = id;
            r.slot = slot;
            r.param = index;
            r.from =
                it != a[i].params.end() ? it->second : value;  // unknown: the engine reads the live value
            r.to = value;
            out.push_back(r);
        }
    }
}

void diffChain(std::vector<Ramp>& out, const SpeakerChainSpec& a, const SpeakerChainSpec& b) {
    const int id = b.speakerId;
    addScalar(out, RampTarget::SpeakerTrimDb, id, a.trimDb, b.trimDb);
    addScalar(out, RampTarget::SpeakerMute, id, a.mute ? 1.0f : 0.0f, b.mute ? 1.0f : 0.0f);
    addScalar(out, RampTarget::SpeakerUserDelayMs, id, a.userDelayMs, b.userDelayMs);
    addScalar(out, RampTarget::SpeakerPolarity, id, a.polarityInverted ? 1.0f : 0.0f,
              b.polarityInverted ? 1.0f : 0.0f);
    addScalar(out, RampTarget::SpeakerEqLowFreq, id, a.eq.lowFreq, b.eq.lowFreq);
    addScalar(out, RampTarget::SpeakerEqLowGainDb, id, a.eq.lowGainDb, b.eq.lowGainDb);
    addScalar(out, RampTarget::SpeakerEqMidFreq, id, a.eq.midFreq, b.eq.midFreq);
    addScalar(out, RampTarget::SpeakerEqMidGainDb, id, a.eq.midGainDb, b.eq.midGainDb);
    addScalar(out, RampTarget::SpeakerEqMidQ, id, a.eq.midQ, b.eq.midQ);
    addScalar(out, RampTarget::SpeakerEqHighFreq, id, a.eq.highFreq, b.eq.highFreq);
    addScalar(out, RampTarget::SpeakerEqHighGainDb, id, a.eq.highGainDb, b.eq.highGainDb);
}

bool contains(const std::vector<int>& v, int x) {
    return std::find(v.begin(), v.end(), x) != v.end();
}

}  // namespace

bool sameInsertStructure(const std::vector<PluginSlot>& a, const std::vector<PluginSlot>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i].pluginId != b[i].pluginId || a[i].stateBase64 != b[i].stateBase64) return false;
    return true;
}

SpeakerChainSpec effectiveChain(const Scene& scene, const Speaker& speaker) {
    if (const auto* c = findChain(scene, speaker.id)) return *c;
    SpeakerChainSpec c;
    c.speakerId = speaker.id;
    c.trimDb = speaker.trimDb;
    c.userDelayMs = speaker.userDelayMs;
    c.polarityInverted = speaker.polarityInverted;
    return c;
}

SceneDiff diffScenes(const Scene& current, const Scene& target, const Layout& layout) {
    SceneDiff diff;

    for (const auto& t : target.sources) {
        const SourceSpec* c = findSource(current, t.id);
        if (c == nullptr) {
            diff.addedSources.push_back(t.id);
        } else if (!sameStructure(*c, t)) {
            diff.replacedSources.push_back(t.id);
        } else {
            diffSource(diff.ramps, *c, t);
            if (sameInsertStructure(c->inserts, t.inserts))
                diffInserts(diff.ramps, false, t.id, c->inserts, t.inserts);
            else
                diff.sourceInsertChanges.push_back(t.id);
        }
    }
    for (const auto& c : current.sources)
        if (findSource(target, c.id) == nullptr) diff.removedSources.push_back(c.id);

    for (const auto& sp : layout.speakers) {
        const SpeakerChainSpec a = effectiveChain(current, sp);
        const SpeakerChainSpec b = effectiveChain(target, sp);
        diffChain(diff.ramps, a, b);
        if (sameInsertStructure(a.inserts, b.inserts))
            diffInserts(diff.ramps, true, sp.id, a.inserts, b.inserts);
        else
            diff.speakerInsertChanges.push_back(sp.id);
    }

    addScalar(diff.ramps, RampTarget::MasterGainDb, 0, current.master.gainDb, target.master.gainDb);
    return diff;
}

Scene intermediateScene(const Scene& current, const Scene& target, const SceneDiff& diff,
                        const Layout& layout) {
    Scene out;
    out.name = target.name;
    out.master = current.master;  // not part of any graph; the master ramp moves it

    for (const auto& t : target.sources) {
        const SourceSpec* c = findSource(current, t.id);
        const bool persists = c != nullptr && !contains(diff.replacedSources, t.id);
        out.sources.push_back(persists ? withContinuousFrom(t, *c) : t);
    }

    for (const auto& sp : layout.speakers) {
        if (findChain(current, sp.id) == nullptr && findChain(target, sp.id) == nullptr) continue;
        SpeakerChainSpec t = effectiveChain(target, sp);
        const SpeakerChainSpec c = effectiveChain(current, sp);
        t.trimDb = c.trimDb;
        t.userDelayMs = c.userDelayMs;
        t.polarityInverted = c.polarityInverted;
        t.mute = c.mute;
        t.eq = c.eq;
        if (sameInsertStructure(c.inserts, t.inserts)) t.inserts = c.inserts;
        out.speakerChains.push_back(std::move(t));
    }
    return out;
}

Scene captureScene(const Project& project, std::string name) {
    Scene s;
    s.name = std::move(name);
    s.sources = project.sources;
    s.speakerChains = project.speakerChains;
    s.master = project.master;
    return s;
}

void applyScene(Project& project, const Scene& scene) {
    project.sources = scene.sources;
    project.speakerChains = scene.speakerChains;
    project.master = scene.master;
}

const Scene* findScene(const Project& project, const std::string& name) {
    for (const auto& s : project.scenes)
        if (s.name == name) return &s;
    return nullptr;
}

}  // namespace spacer
