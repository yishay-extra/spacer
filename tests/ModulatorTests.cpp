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

// ---- WP14: motionRight / insert paths, live edits, phase continuity ----------

TEST_CASE("ParameterPath: motionRight.* and insert.{slot}.param.{index} (WP14)") {
    const auto r = ParameterPath::parse("motionRight.radius");
    REQUIRE(r);
    CHECK(r->domain == ParameterPath::Domain::MotionRight);
    CHECK(r->motion == MotionParam::Radius);
    CHECK(r->toString() == "motionRight.radius");
    for (std::size_t i = 0; i < kNumMotionParams; ++i) {
        ParameterPath p;
        p.domain = ParameterPath::Domain::Motion;
        p.motion = static_cast<MotionParam>(i);
        const std::string right = "motionRight." + std::string(p.toString()).substr(7);
        const auto parsed = ParameterPath::parse(right);
        REQUIRE(parsed);
        CHECK(parsed->motion == p.motion);
        CHECK(parsed->toString() == right);
    }
    const auto ins = ParameterPath::parse("insert.2.param.17");
    REQUIRE(ins);
    CHECK(ins->domain == ParameterPath::Domain::Insert);
    CHECK(ins->slot == 2);
    CHECK(ins->index == 17);
    CHECK(ins->toPathString() == "insert.2.param.17");
    CHECK_FALSE(ParameterPath::parse("motionRight.bogus"));
    CHECK_FALSE(ParameterPath::parse("insert.9.param.0"));  // slot out of range
    CHECK_FALSE(ParameterPath::parse("motionRight."));
}

TEST_CASE("ModulatorBank: motionRight and insert targets have their own outputs (WP14)") {
    ModulatorBank bank;
    bank.configure({ModulatorSpec{OscSource{"/a"}, "motionRight.radius", 1.0f, 0.0f},
                    ModulatorSpec{OscSource{"/b"}, "insert.1.param.3", 0.5f, 0.0f},
                    ModulatorSpec{OscSource{"/c"}, "insert.1.param.3", 1.0f, 0.1f},
                    ModulatorSpec{OscSource{"/d"}, "motion.radius", 1.0f, 0.0f}});
    bank.setExternal(0, 2.0f);
    bank.setExternal(1, 0.2f);
    bank.setExternal(2, 0.1f);
    bank.setExternal(3, 3.0f);
    ClockDriver d;
    const auto out = bank.process(d.next());
    CHECK(out.motionRight[MotionParam::Radius] == Approx(2.0f));
    CHECK(out.motion[MotionParam::Radius] == Approx(3.0f));
    REQUIRE(out.numInserts == 1);
    CHECK(out.inserts[0].slot == 1);
    CHECK(out.inserts[0].index == 3);
    CHECK(out.inserts[0].value == Approx(0.5f * 0.2f + (0.1f + 0.1f)));
}

TEST_CASE("ModulatorBank: a live edit keeps the LFO phase, also across a period change (WP14)") {
    ModulatorBank live, reference;
    live.configure({lfo(LfoShape::Saw, Period::beats(4), "gain", 1.0f)});
    reference.configure({lfo(LfoShape::Saw, Period::beats(4), "gain", 2.0f)});
    ClockDriver d;
    for (int i = 0; i < kBlocksPerBeat; ++i) {  // beat 1: saw at phase 0.25
        const MotionContext c = d.next();
        live.process(c);
        reference.process(c);
    }
    CHECK(live.value(0) == Approx(-0.5f));
    // Depth edit: same phase as a bank that always had depth 2.
    live.setLive(0, ModulatorBank::Live::fromSpec(lfo(LfoShape::Saw, Period::beats(4), "gain", 2.0f)), 1);
    MotionContext c = d.next();
    live.process(c);
    reference.process(c);
    CHECK(live.value(0) == Approx(reference.value(0)));
    // Period edit 4 -> 8 beats: the phase continues from where it was (no jump).
    const float before = live.value(0);
    live.setLive(0, ModulatorBank::Live::fromSpec(lfo(LfoShape::Saw, Period::beats(8), "gain", 2.0f)), 1);
    live.process(d.next());
    // One block = 0.02 beats: saw slope 2·depth per period -> 4 / 8 · 0.02.
    CHECK(live.value(0) - before == Approx(4.0f / 8.0f * 0.02f).margin(1e-4));
}

TEST_CASE("ModulatorBank: an appended LFO starts at phase 0, truncation drops (WP14)") {
    ModulatorBank bank;
    bank.configure({lfo(LfoShape::Saw, Period::beats(4), "gain", 1.0f)});
    ClockDriver d;
    for (int i = 0; i < 77; ++i) bank.process(d.next());
    bank.setLive(1, ModulatorBank::Live::fromSpec(lfo(LfoShape::Saw, Period::beats(4), "spread", 1.0f)), 2);
    CHECK(bank.size() == 2);
    bank.process(d.next());
    CHECK(bank.value(1) == Approx(-1.0f + 2.0f * 0.02f / 4.0f));  // one block after phase 0
    bank.setLive(-1, {}, 1);
    CHECK(bank.size() == 1);
    CHECK_FALSE(bank.targets(SourceParam::Spread));
    CHECK(bank.targets(SourceParam::GainDb));
}

TEST_CASE("ModulatorBank: continueTimelineFrom keeps phase and external values (WP14)") {
    ModulatorBank old, next;
    old.configure({lfo(LfoShape::Saw, Period::beats(4), "gain", 1.0f),
                   ModulatorSpec{OscSource{"/x"}, "spread", 1.0f, 0.0f}});
    old.setExternal(1, 0.7f);
    ClockDriver d;
    for (int i = 0; i < kBlocksPerBeat; ++i) old.process(d.next());
    const float phaseValue = old.value(0);
    next.configure({lfo(LfoShape::Saw, Period::beats(2), "gain", 1.0f),
                    ModulatorSpec{OscSource{"/y"}, "spread", 1.0f, 0.0f}});
    next.continueTimelineFrom(old);
    next.process(d.next());
    CHECK(next.value(0) - phaseValue == Approx(2.0f * 0.02f / 2.0f).margin(1e-4));
    CHECK(next.value(1) == Approx(0.7f));
}

TEST_CASE("Trajectory: a re-anchored period change keeps the position continuous (WP14)") {
    OrbitMotion a = orbit();
    OrbitMotion b = orbit();
    b.period = Period::seconds(3);  // other unit too
    const auto runningA = MotionFactory::create(MotionSpec{a});
    ClockDriver d;
    Position last;
    for (int i = 0; i < 37; ++i) last = runningA->evaluate(d.next());
    const auto edited = MotionFactory::create(MotionSpec{b});
    edited->continueTimelineFrom(*runningA);
    edited->reanchorPeriod(a.period, b.period);
    const Position p = edited->evaluate(d.next());
    // One 10 ms block at a 3 s period: 1.2 degrees further.
    CHECK(p.azimuth - last.azimuth == Approx(360.0f * 0.01f / 3.0f).margin(1e-2));
}
