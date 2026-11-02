#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <set>
#include <string>
#include <vector>

#include "AllocationCounter.h"
#include "MotionTestUtil.h"
#include "core/motion/Lfo.h"
#include "core/motion/ModulatorBank.h"
#include "core/motion/MotionFactory.h"
#include "core/motion/OrbitTrajectory.h"
#include "core/motion/ParameterPath.h"

using Catch::Approx;
using namespace spacer;
using namespace spacer::test;

namespace {

constexpr int kBlocksPerBeat = 50;  // 120 bpm, 480-sample blocks

ModulatorSpec lfo(LfoShape shape, Period period, std::string target, float depth, float offset = 0.0f) {
    return ModulatorSpec{LfoSource{shape, period}, std::move(target), depth, offset};
}

OrbitMotion orbit() {
    OrbitMotion o;
    o.centre = {0.0f, 0.0f, 0.0f};
    o.radius = 2.0f;
    o.period = Period::beats(4);
    return o;
}

}  // namespace

// ---- ParameterPath -----------------------------------------------------------

TEST_CASE("ParameterPath parses every documented path and round-trips") {
    const std::vector<std::string> paths = {"gain",
                                            "spread",
                                            "width",
                                            "distance.k",
                                            "distance.rMin",
                                            "distance.q",
                                            "motion.position.az",
                                            "motion.position.el",
                                            "motion.position.dist",
                                            "motion.centre.az",
                                            "motion.centre.el",
                                            "motion.centre.dist",
                                            "motion.radius",
                                            "motion.phaseOffset",
                                            "motion.elevationWobble",
                                            "motion.from.az",
                                            "motion.from.el",
                                            "motion.from.dist",
                                            "motion.to.az",
                                            "motion.to.el",
                                            "motion.to.dist",
                                            "motion.ax",
                                            "motion.ay",
                                            "motion.az",
                                            "motion.phases.x",
                                            "motion.phases.y",
                                            "motion.phases.z",
                                            "motion.stepMetres"};
    std::set<std::string> seen;
    for (const auto& s : paths) {
        INFO(s);
        const auto p = ParameterPath::parse(s);
        REQUIRE(p);
        CHECK(p->toString() == s);
        seen.insert(std::string(p->toString()));
    }
    CHECK(seen.size() == kNumSourceParams + kNumMotionParams);

    const auto radius = ParameterPath::parse("motion.radius");
    CHECK(radius->domain == ParameterPath::Domain::Motion);
    CHECK(radius->motion == MotionParam::Radius);
    const auto gain = ParameterPath::parse("gain");
    CHECK(gain->domain == ParameterPath::Domain::Source);
    CHECK(gain->source == SourceParam::GainDb);
}

TEST_CASE("ParameterPath rejects unknown paths") {
    for (const char* s : {"", "Gain", "motion", "motion.", "motion.period", "distance", "gain.x", " gain"})
        CHECK_FALSE(ParameterPath::parse(s));
}

// ---- LFO ---------------------------------------------------------------------

TEST_CASE("LFO shapes") {
    CHECK(lfoValue(LfoShape::Sine, 0.0) == Approx(0.0f).margin(1e-6));
    CHECK(lfoValue(LfoShape::Sine, 0.25) == Approx(1.0f));
    CHECK(lfoValue(LfoShape::Sine, 2.75) == Approx(-1.0f));
    CHECK(lfoValue(LfoShape::Triangle, 0.0) == Approx(0.0f).margin(1e-6));
    CHECK(lfoValue(LfoShape::Triangle, 0.25) == Approx(1.0f));
    CHECK(lfoValue(LfoShape::Triangle, 0.5) == Approx(0.0f).margin(1e-6));
    CHECK(lfoValue(LfoShape::Triangle, 0.75) == Approx(-1.0f));
    CHECK(lfoValue(LfoShape::Triangle, 0.125) == Approx(0.5f));
    CHECK(lfoValue(LfoShape::Saw, 0.0) == Approx(-1.0f));
    CHECK(lfoValue(LfoShape::Saw, 0.5) == Approx(0.0f).margin(1e-6));
    CHECK(lfoValue(LfoShape::Saw, 1.75) == Approx(0.5f));
    CHECK(lfoValue(LfoShape::Square, 0.2) == 1.0f);
    CHECK(lfoValue(LfoShape::Square, 0.7) == -1.0f);
}

TEST_CASE("LFO sample-and-hold holds per period and depends on the seed") {
    std::set<float> values;
    for (int k = 0; k < 50; ++k) {
        const float v = lfoValue(LfoShape::SampleAndHold, k + 0.1, 5);
        CHECK(v == lfoValue(LfoShape::SampleAndHold, k + 0.9, 5));
        CHECK(v >= -1.0f);
        CHECK(v <= 1.0f);
        values.insert(v);
    }
    CHECK(values.size() == 50);
    CHECK(lfoValue(LfoShape::SampleAndHold, 3.5, 5) != lfoValue(LfoShape::SampleAndHold, 3.5, 6));
}

// ---- ModulatorBank -------------------------------------------------------------

TEST_CASE("LFO modulator adds to motion.radius") {
    ModulatorBank bank;
    // Square LFO, 2-beat period: +1 for the first beat, −1 for the second.
    bank.configure({lfo(LfoShape::Square, Period::beats(2), "motion.radius", 0.5f, 0.25f)});
    const auto motion = MotionFactory::create(MotionSpec{orbit()});
    ClockDriver d;
    Position p;
    for (int i = 0; i < kBlocksPerBeat / 2; ++i) {  // to beat 0.5: θ = 45°, radius 2 + 0.25 + 0.5
        const MotionContext c = d.next();
        bank.applyTo(*motion, c);
        p = motion->evaluate(c);
    }
    CHECK(bank.value(0) == Approx(0.75f));
    CHECK(p.distance == Approx(2.75f).margin(1e-4));
    CHECK(p.azimuth == Approx(45.0f).margin(1e-3));
    for (int i = 0; i < kBlocksPerBeat; ++i) {  // to beat 1.5: radius 2 + 0.25 − 0.5
        const MotionContext c = d.next();
        bank.applyTo(*motion, c);
        p = motion->evaluate(c);
    }
    CHECK(p.distance == Approx(1.75f).margin(1e-4));
}

TEST_CASE("Modulators targeting the same parameter sum") {
    ModulatorBank bank;
    bank.configure({lfo(LfoShape::Square, Period::beats(4), "gain", 3.0f),
                    lfo(LfoShape::Square, Period::beats(4), "gain", 0.0f, -1.0f),
                    lfo(LfoShape::Square, Period::beats(4), "spread", 0.1f)});
    ClockDriver d;
    const auto out = bank.process(d.next());
    CHECK(out.source[SourceParam::GainDb] == Approx(2.0f));
    CHECK(out.source[SourceParam::Spread] == Approx(0.1f));
    CHECK(out.source[SourceParam::DistanceK] == 0.0f);
}

TEST_CASE("OSC and MIDI modulators use the external value") {
    ModulatorBank bank;
    bank.configure({ModulatorSpec{OscSource{"/x"}, "distance.k", 2.0f, 0.5f},
                    ModulatorSpec{MidiCcSource{1}, "motion.radius", 4.0f, -2.0f}});
    ClockDriver d;
    auto out = bank.process(d.next());
    CHECK(out.source[SourceParam::DistanceK] == Approx(0.5f));  // offset only until a value arrives
    bank.setExternal(0, 0.25f);
    bank.setExternal(1, 1.0f);  // cc 127, normalised
    bank.setExternal(7, 1.0f);  // out of range: ignored
    out = bank.process(d.next());
    CHECK(out.source[SourceParam::DistanceK] == Approx(1.0f));
    CHECK(out.motion[MotionParam::Radius] == Approx(2.0f));
}

TEST_CASE("Unknown targets and extra modulators are inactive") {
    ModulatorBank bank;
    std::vector<ModulatorSpec> specs(6, lfo(LfoShape::Square, Period::beats(4), "gain", 1.0f));
    specs[1].target = "motion.nope";
    bank.configure(specs);
    CHECK(bank.size() == kMaxModulators);
    CHECK(bank.active(0));
    CHECK_FALSE(bank.active(1));
    ClockDriver d;
    CHECK(bank.process(d.next()).source[SourceParam::GainDb] == Approx(3.0f));  // 4 slots, one inactive
}

TEST_CASE("Unsupported motion targets are ignored by the motion") {
    const auto line = MotionFactory::create(MotionSpec{LineMotion{}});
    CHECK_FALSE(line->supports(MotionParam::Radius));
    const auto reference = MotionFactory::create(MotionSpec{LineMotion{}});
    MotionModulation m;
    m[MotionParam::Radius] = 5.0f;
    line->setModulation(m);
    ClockDriver d;
    const MotionContext c = d.next();
    CHECK(line->evaluate(c) == reference->evaluate(c));
}

TEST_CASE("LFO phase follows beats, freezes on stop and restarts on reset") {
    ModulatorBank beats, seconds;
    beats.configure({lfo(LfoShape::Saw, Period::beats(4), "gain", 1.0f)});
    seconds.configure({lfo(LfoShape::Saw, Period::seconds(2), "gain", 1.0f)});
    ClockDriver d;
    auto step = [&](int blocks) {
        for (int i = 0; i < blocks; ++i) {
            const MotionContext c = d.next();
            beats.process(c);
            seconds.process(c);
        }
    };
    step(kBlocksPerBeat);  // beat 1 = 0.5 s
    CHECK(beats.value(0) == Approx(-0.5f));
    CHECK(seconds.value(0) == Approx(-0.5f));
    d.clock.stop();
    step(kBlocksPerBeat);
    CHECK(beats.value(0) == Approx(-0.5f));  // frozen
    CHECK(seconds.value(0) == Approx(0.0f).margin(1e-5));
    d.clock.resume();
    beats.reset();
    step(1);
    CHECK(beats.value(0) == Approx(-1.0f + 2.0f * 0.02f / 4.0f));  // one block after phase 0
}

TEST_CASE("ModulatorBank::process does not allocate") {
    ModulatorBank bank;
    bank.configure({lfo(LfoShape::Sine, Period::beats(1), "motion.radius", 1.0f),
                    lfo(LfoShape::SampleAndHold, Period::seconds(0.3), "gain", 3.0f),
                    ModulatorSpec{OscSource{"/a"}, "spread", 1.0f, 0.0f},
                    ModulatorSpec{MidiCcSource{3}, "motion.centre.az", 90.0f, 0.0f}});
    const auto motion = MotionFactory::create(MotionSpec{orbit()});
    ClockDriver d;
    std::vector<MotionContext> contexts;
    contexts.reserve(1000);
    for (int i = 0; i < 1000; ++i) contexts.push_back(d.next());
    long allocs = 0;
    {
        AllocationCounter counter;
        for (const auto& c : contexts) {
            bank.setExternal(2, 0.5f);
            const SourceModulation s = bank.applyTo(*motion, c);
            (void)s;
            motion->evaluate(c);
        }
        bank.reset();
        allocs = counter.count();
    }
    CHECK(allocs == 0);
}
