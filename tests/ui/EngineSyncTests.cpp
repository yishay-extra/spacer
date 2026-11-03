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

TEST_CASE("Missing sources fall back to a rebuild", "[ui][sync]") {
    Project p = makeProject();
    REQUIRE(planEngineSync(p, EngineEffect::source(42)).rebuildGraph);
}

TEST_CASE("Parameter edits of a moving source are commands, without positions", "[ui][sync]") {
    // A gain / mute / spread / distance edit must not rebuild (that would restart the phase).
    Project p = makeProject();
    p.sources[0].motion.value = OrbitMotion{};
    p.sources[0].gainDb = -9.0f;
    const auto plan = planEngineSync(p, EngineEffect::source(3));
    REQUIRE_FALSE(plan.rebuildGraph);
    REQUIRE(findCommand<cmd::SetSourceGain>(plan)->gainDb == -9.0f);
    REQUIRE(findCommand<cmd::SetSourceDistance>(plan));
    REQUIRE(findCommand<cmd::SetSourcePosition>(plan) == nullptr);
}

TEST_CASE("Linked points: only Manual channels get positions", "[ui][sync]") {
    Project p = makeProject();
    p.sources[0].stereo = true;
    p.sources[0].linkedPoints = true;
    p.sources[0].motionRight = MotionSpec{OrbitMotion{}};
    const auto plan = planEngineSync(p, EngineEffect::source(3));
    REQUIRE_FALSE(plan.rebuildGraph);
    int positions = 0;
    for (const auto& c : plan.commands)
        if (const auto* sp = std::get_if<cmd::SetSourcePosition>(&c)) {
            REQUIRE(sp->channel == 0);
            ++positions;
        }
    REQUIRE(positions == 1);
}

TEST_CASE("Live motion parameters become SetMotionParam from the model", "[ui][sync]") {
    Project p = makeProject();
    OrbitMotion orbit;
    orbit.radius = 2.5f;
    p.sources[0].motion.value = orbit;
    auto plan = planEngineSync(p, EngineEffect::motionParam(3, -1, MotionParam::Radius));
    REQUIRE_FALSE(plan.rebuildGraph);
    REQUIRE(plan.commands.size() == 1);
    const auto* c = findCommand<cmd::SetMotionParam>(plan);
    REQUIRE(c);
    REQUIRE(c->sourceId == 3);
    REQUIRE(c->channel == -1);
    REQUIRE(c->param == MotionParam::Radius);
    REQUIRE(c->value == 2.5f);

    // A parameter the motion no longer has (kind changed), or a missing source: rebuild.
    p.sources[0].motion.value = LineMotion{};
    REQUIRE(planEngineSync(p, EngineEffect::motionParam(3, -1, MotionParam::Radius)).rebuildGraph);
    REQUIRE(planEngineSync(p, EngineEffect::motionParam(99, -1, MotionParam::Radius)).rebuildGraph);
    // Channel 1 of a source that has only one point.
    p.sources[0].motion.value = orbit;
    REQUIRE(planEngineSync(p, EngineEffect::motionParam(3, 1, MotionParam::Radius)).rebuildGraph);
}

TEST_CASE("Linked-points motion parameters address their own channel", "[ui][sync]") {
    Project p = makeProject();
    p.sources[0].stereo = true;
    p.sources[0].linkedPoints = true;
    OrbitMotion left, right;
    left.radius = 1.0f;
    right.radius = 4.0f;
    p.sources[0].motion.value = left;
    p.sources[0].motionRight = MotionSpec{right};
    const auto plan = planEngineSync(p, EngineEffect::motionParam(3, 1, MotionParam::Radius));
    REQUIRE(findCommand<cmd::SetMotionParam>(plan)->value == 4.0f);
    REQUIRE(findCommand<cmd::SetMotionParam>(plan)->channel == 1);
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
