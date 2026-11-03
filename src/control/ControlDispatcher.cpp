#include "control/ControlDispatcher.h"

#include <algorithm>
#include <cmath>

#include "core/motion/ParameterPath.h"

namespace spacer {

ControlDispatcher::ControlDispatcher(const Project& project, CueEngine* cues, Config config)
    : project_(project), cues_(cues), config_(std::move(config)) {}

bool ControlDispatcher::send(const Command& c) {
    if (due_ > 0.0 && config_.pushAt) {
        if (config_.pushAt(c, due_)) return true;
    } else if (config_.push && config_.push(c)) {
        return true;
    }
    ++stats_.commandsDropped;
    return false;
}

int ControlDispatcher::resolveSource(const EntityRef& ref) {
    int id = ref.id;
    if (ref.byName()) {
        id = 0;
        for (const auto& s : project_.sources)
            if (s.name == ref.name) {
                id = s.id;
                break;
            }
    } else if (id == 0 && config_.selectedSource) {
        id = config_.selectedSource();
    }
    const bool exists = std::any_of(project_.sources.begin(), project_.sources.end(),
                                    [&](const SourceSpec& s) { return s.id == id; });
    if (!exists) {
        ++stats_.unresolvedTarget;
        return 0;
    }
    return id;
}

int ControlDispatcher::resolveSpeaker(const EntityRef& ref) {
    int id = 0;
    for (const auto& s : project_.layout.speakers)
        if (ref.byName() ? s.name == ref.name : s.id == ref.id) {
            id = s.id;
            break;
        }
    if (id == 0) ++stats_.unresolvedTarget;
    return id;
}

Position ControlDispatcher::currentPosition(int sourceId) {
    if (const auto it = positions_.find(sourceId); it != positions_.end()) return it->second;
    Position p;
    for (const auto& s : project_.sources)
        if (s.id == sourceId)
            if (const auto* manual = std::get_if<ManualMotion>(&s.motion.value)) p = manual->position;
    positions_[sourceId] = p;
    return p;
}

void ControlDispatcher::observePosition(int sourceId, const Position& position) {
    positions_[sourceId] = position;
}

bool ControlDispatcher::setPosition(int sourceId, const Position& p) {
    const Position n = p.normalised();
    positions_[sourceId] = n;
    return send(cmd::SetSourcePosition{sourceId, -1, n, -1.0f});
}

void ControlDispatcher::invalidateState() {
    // A recall rewrites the live project; forget what we cached from it.
    positions_.clear();
    distances_.clear();
}

CueTick ControlDispatcher::tick() const {
    return config_.makeTick ? config_.makeTick() : CueTick{};
}

bool ControlDispatcher::setSourceParam(const EntityRef& ref, const std::string& path, float value) {
    const auto parsed = ParameterPath::parse(path);
    if (!parsed) {
        ++stats_.badPath;
        return false;
    }
    const int id = resolveSource(ref);
    if (id == 0) return false;

    if (parsed->domain == ParameterPath::Domain::Source) {
        const SourceParam sp = parsed->source;
        if (sp == SourceParam::GainDb) return send(cmd::SetSourceGain{id, value, -1.0f});
        if (sp == SourceParam::Spread) return send(cmd::SetSourceSpread{id, std::clamp(value, 0.0f, 1.0f)});
        if (sp == SourceParam::Width) return send(cmd::SetSourceWidth{id, std::clamp(value, 0.0f, 180.0f)});
        auto it = distances_.find(id);
        if (it == distances_.end()) {
            DistanceModel m;
            for (const auto& s : project_.sources)
                if (s.id == id) m = s.distance;
            it = distances_.emplace(id, m).first;
        }
        DistanceModel& m = it->second;
        if (sp == SourceParam::DistanceK) m.k = std::max(value, 0.0f);
        if (sp == SourceParam::DistanceRMin) m.rMin = std::max(value, 0.0f);
        if (sp == SourceParam::DistanceQ) m.q = std::max(value, 0.0f);
        return send(cmd::SetSourceDistance{id, m});
    }

    const MotionParam mp = parsed->motion;
    if (mp == MotionParam::PositionAz || mp == MotionParam::PositionEl || mp == MotionParam::PositionDist) {
        Position p = currentPosition(id);
        (mp == MotionParam::PositionAz   ? p.azimuth
         : mp == MotionParam::PositionEl ? p.elevation
                                         : p.distance) = value;
        return setPosition(id, p);
    }
    if (config_.motionParam) {
        if (config_.motionParam(id, mp, value)) return true;
        ++stats_.unsupported;
        return false;
    }
    return send(cmd::SetMotionParam{id, -1, mp, value});
}

struct ControlDispatcher::Visitor {
    ControlDispatcher& d;

    bool operator()(const ev::SetSourceParam& e) const { return d.setSourceParam(e.source, e.path, e.value); }
    bool operator()(const ev::SetSourcePositionAed& e) const {
        const int id = d.resolveSource(e.source);
        return id != 0 && d.setPosition(id, e.position);
    }
    bool operator()(const ev::SetSourcePositionXyz& e) const {
        const int id = d.resolveSource(e.source);
        return id != 0 && d.setPosition(id, Position::fromCartesian(e.xyz));
    }
    bool operator()(const ev::SetSourceMute& e) const {
        const int id = d.resolveSource(e.source);
        return id != 0 && d.send(cmd::SetSourceMute{id, e.mute, -1.0f});
    }
    bool operator()(const ev::SourceFile& e) const {
        const int id = d.resolveSource(e.source);
        if (id == 0) return false;
        if (d.config_.fileTransport) {
            if (d.config_.fileTransport(id, e.action, e.seconds)) return true;
            ++d.stats_.unsupported;
            return false;
        }
        switch (e.action) {
            case ev::SourceFile::Action::Play:
                return d.send(cmd::FilePlay{id});
            case ev::SourceFile::Action::Stop:
                return d.send(cmd::FileStop{id});
            case ev::SourceFile::Action::Seek:
                return d.send(cmd::FileSeek{id, std::max(0.0, e.seconds)});
        }
        return false;
    }
    bool operator()(const ev::SetSpeakerTrim& e) const {
        const int id = d.resolveSpeaker(e.speaker);
        return id != 0 && d.send(cmd::SetSpeakerTrim{id, e.trimDb, -1.0f});
    }
    bool operator()(const ev::SetSpeakerMute& e) const {
        const int id = d.resolveSpeaker(e.speaker);
        return id != 0 && d.send(cmd::SetSpeakerMute{id, e.mute, -1.0f});
    }
    bool operator()(const ev::SetInsertParam& e) const {
        const int id =
            e.owner == InsertOwner::Source ? d.resolveSource(e.target) : d.resolveSpeaker(e.target);
        if (id == 0 || !std::isfinite(e.value)) return false;
        return d.send(cmd::SetInsertParam{e.owner, id, e.slot, e.index, std::clamp(e.value, 0.0f, 1.0f)});
    }
    bool operator()(const ev::SetMasterGain& e) const { return d.send(cmd::SetMasterGain{e.gainDb, -1.0f}); }
    bool operator()(const ev::Panic& e) const { return d.send(cmd::SetPanic{e.engaged}); }

    bool cueEngine() const {
        if (d.cues_ != nullptr) return true;
        ++d.stats_.unsupported;
        return false;
    }
    bool operator()(const ev::CueGo&) const {
        if (!cueEngine()) return false;
        const bool fired = d.cues_->go(d.tick());
        d.invalidateState();
        return fired;
    }
    bool operator()(const ev::CueBack&) const {
        if (!cueEngine()) return false;
        const bool fired = d.cues_->back(d.tick());
        d.invalidateState();
        return fired;
    }
    bool operator()(const ev::CueJump& e) const {
        if (!cueEngine()) return false;
        int index = e.index;
        if (!e.name.empty()) {
            const auto& cues = d.cues_->sequencer().list().cues;
            const auto it =
                std::find_if(cues.begin(), cues.end(), [&](const Cue& c) { return c.name == e.name; });
            if (it == cues.end()) {
                ++d.stats_.unresolvedTarget;
                return false;
            }
            index = static_cast<int>(it - cues.begin());
        }
        const bool fired = d.cues_->jump(index, d.tick());
        if (!fired) ++d.stats_.unresolvedTarget;
        d.invalidateState();
        return fired;
    }
    bool recall(const std::string& name, float seconds) const {
        if (!cueEngine()) return false;
        const bool ok = d.cues_->recallScene(name, seconds, false);
        if (!ok) ++d.stats_.unresolvedTarget;
        d.invalidateState();
        return ok;
    }
    bool operator()(const ev::RecallScene& e) const { return recall(e.name, e.seconds); }
    bool operator()(const ev::RecallSceneIndex& e) const {
        if (e.index < 0 || e.index >= static_cast<int>(d.project_.scenes.size())) {
            ++d.stats_.unresolvedTarget;
            return false;
        }
        // Copy: recallScene() rewrites the project this name points into.
        const std::string name = d.project_.scenes[static_cast<std::size_t>(e.index)].name;
        return recall(name, e.seconds);
    }

    bool operator()(const ev::ClockBpm& e) const { return d.send(cmd::ClockSetBpm{e.bpm}); }
    bool operator()(const ev::ClockPlay&) const { return d.send(cmd::ClockPlay{}); }
    bool operator()(const ev::ClockStop&) const { return d.send(cmd::ClockStop{}); }
    bool operator()(const ev::ClockContinue&) const { return d.send(cmd::ClockContinue{}); }
    bool operator()(const ev::ClockBeat& e) const { return d.send(cmd::ClockSetBeat{e.beat}); }

    bool operator()(const ev::OscTrigger& e) const {
        const bool fired = d.cues_ != nullptr && d.cues_->onOsc(e.address, d.tick());
        if (fired)
            d.invalidateState();
        else
            ++d.stats_.unknownAddress;
        return fired;
    }
    bool operator()(const ev::MidiTrigger& e) const {
        if (d.cues_ == nullptr) return false;
        const auto type =
            e.type == ev::MidiTrigger::Type::Note ? trigger::Midi::Type::Note : trigger::Midi::Type::Cc;
        const bool fired = d.cues_->onMidi(type, e.number, e.channel, d.tick());
        if (fired) d.invalidateState();
        return fired;
    }
};

void ControlDispatcher::dispatch(const ControlEvent& event, double dueEpochSeconds) {
    ++stats_.dispatched;
    due_ = dueEpochSeconds;
    std::visit(Visitor{*this}, event);
    due_ = 0.0;
}

}  // namespace spacer
