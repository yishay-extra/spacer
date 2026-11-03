#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "MotionTestUtil.h"
#include "core/motion/ModulatorBank.h"
#include "core/motion/MotionFactory.h"
#include "core/motion/MotionParamValue.h"

using Catch::Approx;
using namespace spacer;
using namespace spacer::test;

TEST_CASE("motionParamValue returns the spec value of parameters the kind has") {
    OrbitMotion o;
    o.centre = {10.0f, 20.0f, 3.0f};
    o.radius = 2.5f;
    o.phaseOffset = 45.0f;
    const MotionSpec orbit{o};
    CHECK(motionParamValue(orbit, MotionParam::CentreAz) == 10.0f);
    CHECK(motionParamValue(orbit, MotionParam::CentreEl) == 20.0f);
    CHECK(motionParamValue(orbit, MotionParam::Radius) == 2.5f);
    CHECK(motionParamValue(orbit, MotionParam::PhaseOffset) == 45.0f);
    CHECK_FALSE(motionParamValue(orbit, MotionParam::FromAz).has_value());

    LissajousMotion l;
    l.ay = 4.0f;
    l.phases = {1.0f, 2.0f, 3.0f};
    CHECK(motionParamValue(MotionSpec{l}, MotionParam::Ay) == 4.0f);
    CHECK(motionParamValue(MotionSpec{l}, MotionParam::PhaseZ) == 3.0f);

    LineMotion line;
    CHECK(motionParamValue(MotionSpec{line}, MotionParam::ToAz) == line.to.azimuth);
    CHECK(motionParamValue(MotionSpec{ManualMotion{}}, MotionParam::PositionDist) == 1.0f);
    CHECK(motionParamValue(MotionSpec{RandomWalkMotion{}}, MotionParam::StepMetres) == 0.5f);
    CHECK_FALSE(motionParamValue(MotionSpec{PathMotion{}}, MotionParam::Radius).has_value());
    CHECK_FALSE(motionParamValue(MotionSpec{StepSequenceMotion{}}, MotionParam::Radius).has_value());
}

TEST_CASE("Trajectory::continueTimelineFrom keeps the phase of the motion it replaces") {
    OrbitMotion o;
    o.radius = 2.0f;
    o.period = Period::seconds(1.0);
    auto running = MotionFactory::create(MotionSpec{o});
    ClockDriver clock;  // 10 ms blocks
    for (int i = 0; i < 25; ++i) running->evaluate(clock.next());

    o.radius = 3.0f;
    auto edited = MotionFactory::create(MotionSpec{o});
    auto fresh = MotionFactory::create(MotionSpec{o});
    edited->continueTimelineFrom(*running);
    const MotionContext ctx = clock.next();
    const Position p = edited->evaluate(ctx);
    CHECK(p.distance == Approx(3.0f).margin(1e-4));
    CHECK(p.azimuth == Approx(93.6f).margin(1e-3));  // 26 blocks of 10 ms at 360°/s
    CHECK(fresh->evaluate(ctx).azimuth == Approx(3.6f).margin(1e-3));
}

TEST_CASE("ModulatorBank::continueTimelineFrom keeps the LFO phase") {
    ModulatorSpec m;
    m.source = LfoSource{LfoShape::Saw, Period::seconds(1.0)};
    m.target = "spread";
    ModulatorBank a, b;
    a.configure({m});
    ClockDriver clock;
    for (int i = 0; i < 30; ++i) a.process(clock.next());
    m.depth = 2.0f;
    b.configure({m});
    b.continueTimelineFrom(a);
    const MotionContext ctx = clock.next();
    ModulatorBank fresh;
    fresh.configure({m});
    CHECK(b.process(ctx).source[SourceParam::Spread] !=
          Approx(fresh.process(ctx).source[SourceParam::Spread]));
    CHECK(b.value(0) == Approx(2.0f * a.process(ctx).source[SourceParam::Spread]).margin(1e-5));
}
