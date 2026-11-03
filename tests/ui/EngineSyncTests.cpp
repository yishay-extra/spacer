#include <catch2/catch_test_macros.hpp>

#include "core/LayoutPresets.h"
#include "ui/logic/EngineSync.h"

using namespace spacer;

namespace {

Project makeProject() {
    Project p;
    p.layout = *layoutpresets::load("stereo");
    SourceSpec s;
    s.id = 3;
    s.gainDb = -4.0f;
    s.mute = true;
    s.spread = 0.25f;
    s.motion.value = ManualMotion{{30, 10, 2}, 20.0f};
    p.sources.push_back(s);
    return p;
}

template <typename T>
const T* findCommand(const EngineSyncPlan& plan) {
    for (const auto& c : plan.commands)
        if (const auto* t = std::get_if<T>(&c)) return t;
    return nullptr;
}

}  // namespace

TEST_CASE("Structural edits rebuild the graph", "[ui][sync]") {
    const auto plan = planEngineSync(makeProject(), EngineEffect::structural());
    REQUIRE(plan.rebuildGraph);
    REQUIRE(plan.commands.empty());
    REQUIRE_FALSE(planEngineSync(makeProject(), EngineEffect::none()).rebuildGraph);
}

TEST_CASE("Manual source edits become parameter commands from the model", "[ui][sync]") {
    const auto plan = planEngineSync(makeProject(), EngineEffect::source(3));
    REQUIRE_FALSE(plan.rebuildGraph);
    const auto* pos = findCommand<cmd::SetSourcePosition>(plan);
    REQUIRE(pos);
    REQUIRE(pos->sourceId == 3);
    REQUIRE(pos->channel == -1);
    REQUIRE(pos->position == Position{30, 10, 2});
    REQUIRE(findCommand<cmd::SetSourceGain>(plan)->gainDb == -4.0f);
    REQUIRE(findCommand<cmd::SetSourceMute>(plan)->mute);
    REQUIRE(findCommand<cmd::SetSourceSpread>(plan)->spread == 0.25f);
}

TEST_CASE("Linked points send one position per channel", "[ui][sync]") {
    Project p = makeProject();
    p.sources[0].stereo = true;
    p.sources[0].linkedPoints = true;
    p.sources[0].motionRight = MotionSpec{ManualMotion{{-30, 0, 2}, 20.0f}};
    const auto plan = planEngineSync(p, EngineEffect::source(3));
    int seen = 0;
    for (const auto& c : plan.commands)
        if (const auto* sp = std::get_if<cmd::SetSourcePosition>(&c)) {
            REQUIRE(sp->channel == seen);
            REQUIRE(sp->position == (seen == 0 ? Position{30, 10, 2} : Position{-30, 0, 2}));
            ++seen;
        }
    REQUIRE(seen == 2);
}

TEST_CASE("Moving or missing sources fall back to a rebuild", "[ui][sync]") {
    Project p = makeProject();
    REQUIRE(planEngineSync(p, EngineEffect::source(42)).rebuildGraph);
    p.sources[0].motion.value = OrbitMotion{};
    REQUIRE(planEngineSync(p, EngineEffect::source(3)).rebuildGraph);
}

TEST_CASE("Speaker effects use the chain when there is one", "[ui][sync]") {
    Project p = makeProject();
    const int id = p.layout.speakers[0].id;
    p.layout.speakers[0].trimDb = -1.0f;
    auto plan = planEngineSync(p, EngineEffect::speaker(id));
    REQUIRE(findCommand<cmd::SetSpeakerTrim>(plan)->trimDb == -1.0f);
    SpeakerChainSpec chain;
    chain.speakerId = id;
    chain.trimDb = -9.0f;
    chain.mute = true;
    p.speakerChains.push_back(chain);
    plan = planEngineSync(p, EngineEffect::speaker(id));
    REQUIRE(findCommand<cmd::SetSpeakerTrim>(plan)->trimDb == -9.0f);
    REQUIRE(findCommand<cmd::SetSpeakerMute>(plan)->mute);
    REQUIRE(planEngineSync(p, EngineEffect::speaker(999)).rebuildGraph);
}

TEST_CASE("Master effects send the master gain", "[ui][sync]") {
    Project p = makeProject();
    p.master.gainDb = -7.5f;
    const auto plan = planEngineSync(p, EngineEffect::master());
    REQUIRE(findCommand<cmd::SetMasterGain>(plan)->gainDb == -7.5f);
}
