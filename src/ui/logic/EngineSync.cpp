#include "ui/logic/EngineSync.h"

#include "ui/logic/SourceGeometry.h"

namespace spacer {

namespace {

void sourceCommands(const SourceSpec& s, EngineSyncPlan& plan) {
    plan.commands.push_back(cmd::SetSourceGain{s.id, s.gainDb, -1.0f});
    plan.commands.push_back(cmd::SetSourceMute{s.id, s.mute, -1.0f});
    plan.commands.push_back(cmd::SetSourceSpread{s.id, s.spread});
    plan.commands.push_back(cmd::SetSourceWidth{s.id, s.width});
    plan.commands.push_back(cmd::SetSourceDistance{s.id, s.distance});
    if (s.stereo && s.linkedPoints) {
        for (int ch = 0; ch < 2; ++ch) {
            const auto& m = std::get<ManualMotion>(motionForChannel(s, ch).value);
            plan.commands.push_back(cmd::SetSourcePosition{s.id, ch, m.position, -1.0f});
        }
    } else {
        const auto& m = std::get<ManualMotion>(s.motion.value);
        plan.commands.push_back(cmd::SetSourcePosition{s.id, -1, m.position, -1.0f});
    }
}

}  // namespace

EngineSyncPlan planEngineSync(const Project& project, const EngineEffect& effect) {
    EngineSyncPlan plan;
    switch (effect.kind) {
        case EngineEffect::Kind::None:
            break;
        case EngineEffect::Kind::Structural:
            plan.rebuildGraph = true;
            break;
        case EngineEffect::Kind::Source: {
            const SourceSpec* spec = nullptr;
            for (const auto& s : project.sources)
                if (s.id == effect.id) spec = &s;
            if (spec == nullptr || !isManualOnly(*spec)) {
                plan.rebuildGraph = true;
                break;
            }
            sourceCommands(*spec, plan);
            break;
        }
        case EngineEffect::Kind::Speaker: {
            const Speaker* sp = project.layout.findSpeaker(effect.id);
            if (sp == nullptr) {
                plan.rebuildGraph = true;
                break;
            }
            SpeakerChainSpec chain;
            chain.speakerId = sp->id;
            chain.trimDb = sp->trimDb;
            chain.userDelayMs = sp->userDelayMs;
            chain.polarityInverted = sp->polarityInverted;
            for (const auto& c : project.speakerChains)
                if (c.speakerId == sp->id) chain = c;
            plan.commands.push_back(cmd::SetSpeakerTrim{sp->id, chain.trimDb, -1.0f});
            plan.commands.push_back(cmd::SetSpeakerDelay{sp->id, chain.userDelayMs});
            plan.commands.push_back(cmd::SetSpeakerPolarity{sp->id, chain.polarityInverted});
            plan.commands.push_back(cmd::SetSpeakerMute{sp->id, chain.mute, -1.0f});
            plan.commands.push_back(cmd::SetSpeakerEq{sp->id, chain.eq});
            break;
        }
        case EngineEffect::Kind::Master:
            plan.commands.push_back(cmd::SetMasterGain{project.master.gainDb, -1.0f});
            break;
    }
    return plan;
}

}  // namespace spacer
