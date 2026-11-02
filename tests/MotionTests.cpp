#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <memory>
#include <numbers>
#include <set>
#include <vector>

#include "AllocationCounter.h"
#include "MotionTestUtil.h"
#include "core/motion/LineTrajectory.h"
#include "core/motion/LissajousTrajectory.h"
#include "core/motion/ManualTrajectory.h"
#include "core/motion/MotionFactory.h"
#include "core/motion/OrbitTrajectory.h"
#include "core/motion/PathTrajectory.h"
#include "core/motion/RandomWalkTrajectory.h"
#include "core/motion/StepSequenceTrajectory.h"

using Catch::Approx;
using namespace spacer;
using namespace spacer::test;

namespace {

OrbitMotion simpleOrbit() {
    OrbitMotion o;
    o.centre = {0.0f, 0.0f, 0.0f};
    o.radius = 2.0f;
    o.period = Period::beats(4);
    return o;
}

// One block = 0.02 beats at 120 bpm (10 ms); 50 blocks = 1 beat.
constexpr int kBlocksPerBeat = 50;

Step posStep(Position p, Period dur, StepTransition tr = StepTransition::Jump,
             Period glide = Period::beats(0), float prob = 1.0f) {
    Step s;
    s.target = p;
    s.duration = dur;
    s.transition = tr;
    s.glideTime = glide;
    s.probability = prob;
    return s;
}

Position stepPos(int i) {
    return {static_cast<float>(i) * 30.0f - 90.0f, 0.0f, 2.0f};
}

/// Index of the step whose (distinct) target the position is at, or -1.
int whichStep(const Position& p, int n) {
    for (int i = 0; i < n; ++i)
        if ((p.toCartesian() - stepPos(i).toCartesian()).length() < 1e-3f) return i;
    return -1;
}

std::vector<MotionSpec> everyKind() {
    PathMotion path;
    path.points = {{0, 2, 0}, {2, 0, 0}, {0, -2, 1}, {-2, 0, 0}};
    path.interpolation = PathInterpolation::CatmullRom;
    path.constantSpeed = true;
    StepSequenceMotion seq;
    seq.steps = {posStep(stepPos(0), Period::beats(0.5), StepTransition::Glide, Period::beats(0.25), 0.7f),
                 posStep(stepPos(1), Period::seconds(0.3)), posStep(stepPos(2), Period::beats(0.25))};
    seq.order = StepOrder::Random;
    RandomWalkMotion walk;
    walk.stepPeriod = Period::beats(0.5);
    return {MotionSpec{ManualMotion{}},
            MotionSpec{simpleOrbit()},
            MotionSpec{LineMotion{}},
            MotionSpec{LissajousMotion{}},
            MotionSpec{path},
            MotionSpec{walk},
            MotionSpec{seq}};
}

}  // namespace

// ---- Factory ---------------------------------------------------------------

TEST_CASE("MotionFactory builds the evaluator for every kind") {
    const char* names[] = {"Manual", "Orbit", "Line", "Lissajous", "Path", "RandomWalk", "StepSequence"};
    const auto specs = everyKind();
    for (std::size_t i = 0; i < specs.size(); ++i) {
        const auto m = MotionFactory::create(specs[i]);
        REQUIRE(m);
        CHECK(m->name() == names[i]);
    }
}

// ---- Manual ----------------------------------------------------------------

TEST_CASE("Manual holds its position") {
    ManualTrajectory m(ManualMotion{{30.0f, 10.0f, 3.0f}, 20.0f});
    requireNear(m.current(), Position{30.0f, 10.0f, 3.0f});
    ClockDriver d;
    requireNear(run(m, d, 10), Position{30.0f, 10.0f, 3.0f});
}

TEST_CASE("Manual glides to a new target over smoothMs") {
    ManualTrajectory m(ManualMotion{{0.0f, 0.0f, 2.0f}, 100.0f});
    ClockDriver d(120.0, /*playing*/ false);  // smoothing runs on engine time
    run(m, d, 1);
    m.setTarget({90.0f, 0.0f, 2.0f});
    const Cartesian a{0.0f, 2.0f, 0.0f}, b{2.0f, 0.0f, 0.0f};
    requireNear(run(m, d, 5), a + (b - a) * 0.5f);  // 50 ms of 100 ms
    requireNear(run(m, d, 5), b);
    requireNear(run(m, d, 5), b);
}

TEST_CASE("Manual with smoothMs 0 jumps") {
    ManualTrajectory m(ManualMotion{{0.0f, 0.0f, 2.0f}, 0.0f});
    ClockDriver d;
    run(m, d, 1);
    m.setTarget({-90.0f, 0.0f, 2.0f});
    requireNear(run(m, d, 1), Position{-90.0f, 0.0f, 2.0f});
}

// ---- Orbit -----------------------------------------------------------------

TEST_CASE("Orbit matches the formula at known times") {
    OrbitTrajectory o(simpleOrbit());
    requireNear(o.current(), Cartesian{0.0f, 2.0f, 0.0f});  // θ = 0 → front
    ClockDriver d;
    requireNear(run(o, d, kBlocksPerBeat), Cartesian{2.0f, 0.0f, 0.0f});   // beat 1: right (clockwise)
    requireNear(run(o, d, kBlocksPerBeat), Cartesian{0.0f, -2.0f, 0.0f});  // beat 2: back
    requireNear(run(o, d, kBlocksPerBeat), Cartesian{-2.0f, 0.0f, 0.0f});  // beat 3: left
    requireNear(run(o, d, kBlocksPerBeat), Cartesian{0.0f, 2.0f, 0.0f});   // beat 4: front again

    // Arbitrary time against θ = 2π·t/period.
    const Position p = run(o, d, 37);
    const float theta = 2.0f * kPi * static_cast<float>(37 * 0.02 / 4.0);
    requireNear(p, Cartesian{2.0f * std::sin(theta), 2.0f * std::cos(theta), 0.0f});
}

TEST_CASE("Orbit direction, phase offset and centre") {
    auto spec = simpleOrbit();
    spec.direction = -1;
    spec.phaseOffset = 90.0f;
    spec.centre = {0.0f, 0.0f, 3.0f};  // 3 m in front
    OrbitTrajectory o(spec);
    // θ(0) = 90° → centre + (2, 0, 0)
    requireNear(o.current(), Cartesian{2.0f, 3.0f, 0.0f});
    ClockDriver d;
    // One beat later θ = −90° + 90° = 0 → centre + (0, 2, 0)
    requireNear(run(o, d, kBlocksPerBeat), Cartesian{0.0f, 5.0f, 0.0f});
}

TEST_CASE("Orbit plane is rotated to the centre's direction") {
    auto spec = simpleOrbit();
    spec.centre = {0.0f, 30.0f, 0.0f};  // centre at the origin, tilted 30° up at the front
    OrbitTrajectory o(spec);
    requireNear(o.current(), Position{0.0f, 30.0f, 2.0f});  // front of the tilted plane
    ClockDriver d;
    requireNear(run(o, d, kBlocksPerBeat), Position{90.0f, 0.0f, 2.0f});     // tilt axis
    requireNear(run(o, d, kBlocksPerBeat), Position{180.0f, -30.0f, 2.0f});  // back is lowered

    spec.centre = {90.0f, 0.0f, 0.0f};  // yaw only: θ = 0 points right
    OrbitTrajectory yawed(spec);
    requireNear(yawed.current(), Position{90.0f, 0.0f, 2.0f});
}

TEST_CASE("Orbit elevation wobble") {
    auto spec = simpleOrbit();
    spec.elevationWobble = 30.0f;
    spec.wobblePeriod = Period::beats(4);
    OrbitTrajectory o(spec);
    ClockDriver d;
    // beat 1: θ = 90°, wobble = 30°·sin(90°) = 30°
    requireNear(run(o, d, kBlocksPerBeat), Position{90.0f, 30.0f, 2.0f});
    // beat 3: θ = 270°, wobble = 30°·sin(270°) = −30°
    requireNear(run(o, d, 2 * kBlocksPerBeat), Position{-90.0f, -30.0f, 2.0f});
}

// ---- Line ------------------------------------------------------------------

TEST_CASE("Line modes") {
    LineMotion spec;
    spec.from = {-90.0f, 0.0f, 2.0f};  // (−2, 0, 0)
    spec.to = {90.0f, 0.0f, 2.0f};     // ( 2, 0, 0)
    spec.period = Period::beats(2);

    auto at = [&](PlayMode mode, double beats) {
        spec.mode = mode;
        LineTrajectory l(spec);
        ClockDriver d;
        return run(l, d, static_cast<int>(std::lround(beats * kBlocksPerBeat)));
    };
    requireNear(at(PlayMode::Once, 0.5), Cartesian{-1.0f, 0.0f, 0.0f});
    requireNear(at(PlayMode::Once, 1.0), Cartesian{0.0f, 0.0f, 0.0f});
    requireNear(at(PlayMode::Once, 3.0), Cartesian{2.0f, 0.0f, 0.0f});  // stays at `to`
    requireNear(at(PlayMode::Loop, 2.5), Cartesian{-1.0f, 0.0f, 0.0f});
    requireNear(at(PlayMode::PingPong, 3.0), Cartesian{0.0f, 0.0f, 0.0f});
    requireNear(at(PlayMode::PingPong, 3.5), Cartesian{-1.0f, 0.0f, 0.0f});  // coming back
    requireNear(at(PlayMode::PingPong, 4.5), Cartesian{-1.0f, 0.0f, 0.0f});  // going out again
}

TEST_CASE("Line smooth easing") {
    LineMotion spec;
    spec.from = {-90.0f, 0.0f, 2.0f};
    spec.to = {90.0f, 0.0f, 2.0f};
    spec.period = Period::beats(4);
    spec.mode = PlayMode::Once;
    spec.easing = Easing::Smooth;
    LineTrajectory l(spec);
    ClockDriver d;
    const float u = 0.25f, s = u * u * (3.0f - 2.0f * u);
    requireNear(run(l, d, kBlocksPerBeat), Cartesian{-2.0f + 4.0f * s, 0.0f, 0.0f});
    requireNear(run(l, d, kBlocksPerBeat), Cartesian{0.0f, 0.0f, 0.0f});
}

// ---- Lissajous -------------------------------------------------------------

TEST_CASE("Lissajous matches the formula") {
    LissajousMotion spec;
    spec.centre = {0.0f, 0.0f, 1.0f};  // (0, 1, 0)
    spec.ax = 1.5f;
    spec.ay = 0.5f;
    spec.az = 0.25f;
    spec.fx = 1.0f;
    spec.fy = 2.0f;
    spec.fz = 3.0f;
    spec.phases = {0.0f, 90.0f, 45.0f};
    spec.period = Period::seconds(2.0);
    for (int blocks : {0, 13, 50, 77, 200}) {
        LissajousTrajectory m(spec);
        ClockDriver d(120.0, false);  // seconds-based: transport irrelevant
        const Position q = run(m, d, blocks);
        const double t = blocks * 0.01 / 2.0;  // periods
        auto f = [t](float a, float fr, float ph) {
            return a * static_cast<float>(
                           std::sin(2.0 * std::numbers::pi * fr * t + ph * std::numbers::pi / 180.0));
        };
        requireNear(q, Cartesian{f(1.5f, 1, 0), 1.0f + f(0.5f, 2, 90), f(0.25f, 3, 45)});
    }
}

// ---- Path ------------------------------------------------------------------

TEST_CASE("Path loop is closed and visits every point") {
    PathMotion spec;
    spec.points = {{0, 2, 0}, {2, 0, 0}, {0, -2, 0}, {-2, 0, 0}};
    spec.period = Period::beats(4);
    spec.mode = PlayMode::Loop;
    PathTrajectory p(spec);
    REQUIRE(p.numSegments() == 4);
    requireNear(p.current(), Cartesian{0, 2, 0});
    ClockDriver d;
    requireNear(run(p, d, kBlocksPerBeat), Cartesian{2, 0, 0});
    requireNear(run(p, d, kBlocksPerBeat / 2), Cartesian{1, -1, 0});  // middle of segment 2
    requireNear(run(p, d, kBlocksPerBeat / 2), Cartesian{0, -2, 0});
    requireNear(run(p, d, 2 * kBlocksPerBeat), Cartesian{0, 2, 0});  // closed: back at start
}

TEST_CASE("Path constant speed follows arc length") {
    PathMotion spec;
    spec.points = {{0, 0, 0}, {1, 0, 0}, {4, 0, 0}};  // segments of 1 m and 3 m
    spec.period = Period::beats(4);
    spec.mode = PlayMode::Once;

    spec.constantSpeed = false;  // equal time per segment
    PathTrajectory uneven(spec);
    ClockDriver d1;
    requireNear(run(uneven, d1, 2 * kBlocksPerBeat), Cartesian{1, 0, 0});

    spec.constantSpeed = true;
    PathTrajectory even(spec);
    REQUIRE(even.length() == Approx(4.0f).margin(1e-4));
    ClockDriver d2;
    requireNear(run(even, d2, kBlocksPerBeat), Cartesian{1, 0, 0});
    requireNear(run(even, d2, kBlocksPerBeat), Cartesian{2, 0, 0});
    requireNear(run(even, d2, 2 * kBlocksPerBeat), Cartesian{4, 0, 0});
}

TEST_CASE("Path Catmull-Rom passes through its points") {
    PathMotion spec;
    spec.points = {{0, 2, 0}, {2, 1, 1}, {1, -2, 0}, {-2, 0, 0.5f}};
    spec.interpolation = PathInterpolation::CatmullRom;
    for (PlayMode mode : {PlayMode::Once, PlayMode::Loop}) {
        spec.mode = mode;
        PathTrajectory p(spec);
        for (int i = 0; i < 4; ++i) {
            const Cartesian c = p.pointAt(i);
            REQUIRE((c - spec.points[static_cast<std::size_t>(i)]).length() < 1e-5f);
        }
        // Smooth: between points 1 and 2 the curve is not the straight chord.
        const Cartesian mid = p.pointAt(1.5);
        const Cartesian chord = (spec.points[1] + spec.points[2]) * 0.5f;
        REQUIRE((mid - chord).length() > 1e-3f);
    }
}

TEST_CASE("Path ping-pong goes back along the path") {
    PathMotion spec;
    spec.points = {{0, 2, 0}, {2, 0, 0}, {0, -2, 0}};
    spec.period = Period::beats(2);
    spec.mode = PlayMode::PingPong;
    PathTrajectory p(spec);
    ClockDriver d;
    requireNear(run(p, d, 2 * kBlocksPerBeat), Cartesian{0, -2, 0});
    requireNear(run(p, d, kBlocksPerBeat), Cartesian{2, 0, 0});
    requireNear(run(p, d, kBlocksPerBeat), Cartesian{0, 2, 0});
}

// ---- RandomWalk ------------------------------------------------------------

TEST_CASE("RandomWalk takes steps of stepMetres inside the bounds") {
    RandomWalkMotion spec;
    spec.bounds = SphereBounds{{0, 0, 0}, 3.0f};
    spec.stepMetres = 0.5f;
    spec.stepPeriod = Period::beats(1);
    spec.glide = false;
    RandomWalkTrajectory w(spec);
    ClockDriver d;
    int fullSteps = 0;
    for (int k = 0; k < 200; ++k) {
        run(w, d, kBlocksPerBeat);
        const float len = (w.target() - w.previousTarget()).length();
        REQUIRE(len <= 0.5f + 1e-4f);
        if (std::abs(len - 0.5f) < 1e-4f) ++fullSteps;
        REQUIRE(w.contains(w.target()));
        requireNear(w.current(), w.target());  // jump mode: sits on the target
    }
    CHECK(fullSteps > 180);  // only clamped fallbacks may be shorter
}

TEST_CASE("RandomWalk glides from the previous target to the next") {
    RandomWalkMotion spec;
    spec.bounds = BoxBounds{{-2, -2, 0}, {2, 2, 0}};  // flat box: planar walk
    spec.stepPeriod = Period::beats(1);
    spec.glide = true;
    RandomWalkTrajectory w(spec);
    ClockDriver d;
    run(w, d, kBlocksPerBeat + kBlocksPerBeat / 2);  // half-way through step 1
    requireNear(w.current(), (w.previousTarget() + w.target()) * 0.5f);
    for (int k = 0; k < 50; ++k) {
        run(w, d, kBlocksPerBeat);
        REQUIRE(w.target().z == 0.0f);
        REQUIRE(w.contains(w.target()));
    }
}

TEST_CASE("RandomWalk depends on the seed only") {
    RandomWalkMotion spec;
    spec.stepPeriod = Period::beats(0.25);
    RandomWalkTrajectory a(spec), b(spec);
    spec.seed = 2;
    RandomWalkTrajectory c(spec);
    ClockDriver da, db, dc;
    bool differs = false;
    for (int i = 0; i < 300; ++i) {
        const Position pa = a.evaluate(da.next());
        REQUIRE(pa == b.evaluate(db.next()));
        differs = differs || !(pa == c.evaluate(dc.next()));
    }
    CHECK(differs);
}

// ---- StepSequence ----------------------------------------------------------

TEST_CASE("StepSequence forward order and timing") {
    StepSequenceMotion spec;
    spec.steps = {posStep(stepPos(0), Period::beats(1)), posStep(stepPos(1), Period::beats(0.5)),
                  posStep(stepPos(2), Period::beats(1))};
    StepSequenceTrajectory s(spec, nullptr);
    ClockDriver d;
    std::vector<int> seen;
    for (int i = 0; i < 5 * kBlocksPerBeat - 1; ++i) {  // up to just before beat 5
        const int k = whichStep(s.evaluate(d.next()), 3);
        if (seen.empty() || seen.back() != k) seen.push_back(k);
    }
    CHECK(seen == std::vector<int>{0, 1, 2, 0, 1, 2});  // 2.5 beats per cycle, 5 beats

    // Exact boundary: step 1 starts at beat 1, step 2 at beat 1.5.
    StepSequenceTrajectory s2(spec, nullptr);
    ClockDriver d2;
    CHECK(whichStep(run(s2, d2, kBlocksPerBeat - 1), 3) == 0);
    CHECK(whichStep(run(s2, d2, 1), 3) == 1);
    CHECK(whichStep(run(s2, d2, kBlocksPerBeat / 2 - 1), 3) == 1);
    CHECK(whichStep(run(s2, d2, 1), 3) == 2);
}

TEST_CASE("StepSequence ping-pong and one-shot") {
    StepSequenceMotion spec;
    for (int i = 0; i < 3; ++i) spec.steps.push_back(posStep(stepPos(i), Period::beats(1)));
    spec.order = StepOrder::PingPong;

    auto sequence = [&](int beats) {
        StepSequenceTrajectory s(spec, nullptr);
        ClockDriver d;
        std::vector<int> out{whichStep(s.current(), 3)};
        for (int b = 0; b < beats; ++b) out.push_back(whichStep(run(s, d, kBlocksPerBeat), 3));
        return out;
    };
    CHECK(sequence(7) == std::vector<int>{0, 1, 2, 1, 0, 1, 2, 1});
    spec.loop = false;
    CHECK(sequence(7) == std::vector<int>{0, 1, 2, 1, 0, 0, 0, 0});
    spec.order = StepOrder::Forward;
    CHECK(sequence(5) == std::vector<int>{0, 1, 2, 2, 2, 2});
}

TEST_CASE("StepSequence random order is seeded and never repeats a step immediately") {
    StepSequenceMotion spec;
    for (int i = 0; i < 4; ++i) spec.steps.push_back(posStep(stepPos(i), Period::beats(0.1)));
    spec.order = StepOrder::Random;
    spec.seed = 7;

    auto sequence = [&]() {
        StepSequenceTrajectory s(spec, nullptr);
        ClockDriver d;
        std::vector<int> out;
        for (int i = 0; i < 2000; ++i) {
            const int k = whichStep(s.evaluate(d.next()), 4);
            if (out.empty() || out.back() != k) out.push_back(k);
        }
        return out;
    };
    const auto a = sequence();
    CHECK(a == sequence());
    REQUIRE(a.size() > 300);  // one new step per 5 blocks, no repeats collapsed
    std::set<int> used(a.begin(), a.end());
    CHECK(used == std::set<int>{0, 1, 2, 3});
    spec.seed = 8;
    CHECK(a != sequence());
}

TEST_CASE("StepSequence probability is honoured deterministically") {
    StepSequenceMotion spec;
    spec.steps = {posStep(stepPos(0), Period::beats(0.1)),
                  posStep(stepPos(1), Period::beats(0.1), StepTransition::Jump, Period::beats(0), 0.25f)};
    spec.seed = 3;

    auto countStep1 = [&](long& taken) {
        StepSequenceTrajectory s(spec, nullptr);
        ClockDriver d;
        int visits = 0;
        int prev = 0;
        for (int i = 0; i < 20000; ++i) {
            const int k = whichStep(s.evaluate(d.next()), 2);
            if (k == 1 && prev != 1) ++visits;
            prev = k;
        }
        taken = s.stepsTaken();
        return visits;
    };
    long takenA = 0, takenB = 0;
    const int a = countStep1(takenA);
    CHECK(a == countStep1(takenB));
    CHECK(takenA == takenB);
    // 4000 step-0 passes at 0.1 beat; step 1 follows each with p = 0.25.
    const double ratio = static_cast<double>(a) / (static_cast<double>(takenA) - a);
    CHECK(ratio == Approx(0.25).margin(0.04));

    spec.steps[1].probability = 0.0f;  // never taken
    long t = 0;
    CHECK(countStep1(t) == 0);
}

TEST_CASE("StepSequence glide and speaker targets") {
    Layout layout;
    layout.speakers = {Speaker{1, "L", {-30.0f, 0.0f, 2.0f}}, Speaker{2, "R", {30.0f, 0.0f, 2.0f}}};
    StepSequenceMotion spec;
    Step a;
    a.target = 1;  // speaker L
    a.duration = Period::beats(1);
    Step b = a;
    b.target = 2;  // speaker R
    b.transition = StepTransition::Glide;
    b.glideTime = Period::beats(0.4);  // 20 blocks
    spec.steps = {a, b};

    StepSequenceTrajectory s(spec, &layout);
    const Cartesian l = layout.speakers[0].position.toCartesian();
    const Cartesian r = layout.speakers[1].position.toCartesian();
    requireNear(s.current(), l);
    ClockDriver d;
    requireNear(run(s, d, kBlocksPerBeat), l);  // step 1 begins, glide starts at L
    requireNear(run(s, d, 10), l + (r - l) * 0.5f);
    requireNear(run(s, d, 10), r);
    requireNear(run(s, d, 10), r);  // holds after the glide
    requireNear(run(s, d, 20), l);  // back to step 0 (jump) after one beat

    // Without a layout the speaker steps are unusable: the sequence holds.
    StepSequenceTrajectory none(spec, nullptr);
    CHECK(none.finished());
}

TEST_CASE("StepSequence mixes beats and seconds") {
    StepSequenceMotion spec;
    spec.steps = {posStep(stepPos(0), Period::seconds(0.25)), posStep(stepPos(1), Period::beats(1))};
    StepSequenceTrajectory s(spec, nullptr);
    ClockDriver d(60.0);  // 1 beat = 1 s, 1 block = 0.01 beat
    CHECK(whichStep(run(s, d, 24), 2) == 0);
    CHECK(whichStep(run(s, d, 1), 2) == 1);   // 0.25 s
    CHECK(whichStep(run(s, d, 99), 2) == 1);  // 1.24 s
    CHECK(whichStep(run(s, d, 1), 2) == 0);   // 1.25 s
    d.clock.stop();                           // seconds-based step 0 still ends...
    CHECK(whichStep(run(s, d, 25), 2) == 1);
    CHECK(whichStep(run(s, d, 500), 2) == 1);  // ...but beats-based step 1 is frozen
}

// ---- Clock behaviour (beats vs seconds) -------------------------------------

TEST_CASE("Beats-based periods follow tempo changes") {
    OrbitTrajectory o(simpleOrbit());  // 4-beat period
    ClockDriver d(120.0);
    run(o, d, kBlocksPerBeat);                                  // beat 1
    d.clock.setBpm(60.0);                                       // now 0.01 beat per block
    requireNear(run(o, d, 100), Cartesian{0.0f, -2.0f, 0.0f});  // beat 2
    d.clock.setBpm(240.0);                                      // 0.04 beat per block
    requireNear(run(o, d, 25), Cartesian{-2.0f, 0.0f, 0.0f});   // beat 3
}

TEST_CASE("Beats-based motion freezes on stop; seconds-based does not") {
    auto beatsSpec = simpleOrbit();
    auto secondsSpec = simpleOrbit();
    secondsSpec.period = Period::seconds(2.0);  // same speed as 4 beats at 120 bpm
    OrbitTrajectory beats(beatsSpec), seconds(secondsSpec);
    ClockDriver d;
    for (int i = 0; i < kBlocksPerBeat; ++i) {
        const MotionContext c = d.next();
        beats.evaluate(c);
        seconds.evaluate(c);
    }
    requireNear(beats.current(), Cartesian{2.0f, 0.0f, 0.0f});
    requireNear(seconds.current(), Cartesian{2.0f, 0.0f, 0.0f});

    d.clock.stop();
    for (int i = 0; i < kBlocksPerBeat; ++i) {  // 0.5 s stopped
        const MotionContext c = d.next();
        beats.evaluate(c);
        seconds.evaluate(c);
    }
    requireNear(beats.current(), Cartesian{2.0f, 0.0f, 0.0f});     // frozen
    requireNear(seconds.current(), Cartesian{0.0f, -2.0f, 0.0f});  // kept going

    d.clock.setBpm(60.0);  // tempo does not affect seconds
    d.clock.resume();
    for (int i = 0; i < kBlocksPerBeat; ++i) {
        const MotionContext c = d.next();
        beats.evaluate(c);
        seconds.evaluate(c);
    }
    // 0.5 s at 60 bpm = 0.5 beat: beat 1.5 → θ = 2π·1.5/4
    requireNear(beats.current(), Cartesian{std::sin(0.75f * kPi) * 2.0f, std::cos(0.75f * kPi) * 2.0f, 0.0f});
    requireNear(seconds.current(), Cartesian{-2.0f, 0.0f, 0.0f});
}

TEST_CASE("Motion catches up one block after a quantised start inside a block") {
    // ClockState has no sub-block start offset (PLAN WP6 notes). The motion
    // stays frozen for the block containing the start and is exact again from
    // the next block, because phase comes from clock.beat.
    OrbitTrajectory o(simpleOrbit());
    InternalClock clock(kRate);
    clock.setBpm(120.0);
    clock.setBeat(3.99);  // start lands 0.01 beat (half a block) into the next block
    clock.setQuantisedStart(true);
    clock.play();
    REQUIRE(clock.armed());
    const Position frozen = o.evaluate({clock.advance(kBlock), kBlock, kRate});  // origin = beat 3.99
    requireNear(frozen, Cartesian{0.0f, 2.0f, 0.0f});
    // Next block starts at beat 4.01, ends at 4.03: 0.04 beats after the origin.
    const Position p = o.evaluate({clock.advance(kBlock), kBlock, kRate});
    const float theta = 2.0f * kPi * 0.04f / 4.0f;
    requireNear(p, Cartesian{2.0f * std::sin(theta), 2.0f * std::cos(theta), 0.0f});
}

// ---- reset / determinism / RT safety ----------------------------------------

TEST_CASE("reset() restarts the phase for every kind") {
    for (const MotionSpec& spec : everyKind()) {
        const auto m = MotionFactory::create(spec);
        INFO(m->name());
        ClockDriver d;
        run(*m, d, 123);
        m->reset();
        // After reset the motion behaves like a fresh one started at this block.
        const auto fresh = MotionFactory::create(spec);
        ClockDriver d2;
        d2.clock.setBeat(d.clock.current().beat);
        for (int i = 0; i < 200; ++i) {
            const MotionContext c = d.next();
            MotionContext c2 = c;
            requireNear(m->evaluate(c), fresh->evaluate(c2), 1e-4f);
        }
    }
}

TEST_CASE("Every kind is deterministic for a fixed sequence of contexts") {
    for (const MotionSpec& spec : everyKind()) {
        const auto a = MotionFactory::create(spec);
        const auto b = MotionFactory::create(spec);
        INFO(a->name());
        ClockDriver d;
        for (int i = 0; i < 1000; ++i) {
            if (i == 400) d.clock.setBpm(97.0);
            if (i == 600) d.clock.stop();
            if (i == 700) d.clock.resume();
            const MotionContext c = d.next();
            REQUIRE(a->evaluate(c) == b->evaluate(c));
        }
    }
}

TEST_CASE("evaluate() does not allocate") {
    Layout layout;
    layout.speakers = {Speaker{1, "L", {-30.0f, 0.0f, 2.0f}}, Speaker{2, "R", {30.0f, 0.0f, 2.0f}}};
    std::vector<std::unique_ptr<Trajectory>> motions;
    for (const MotionSpec& spec : everyKind()) motions.push_back(MotionFactory::create(spec, &layout));
    ClockDriver d;
    std::vector<MotionContext> contexts;
    contexts.reserve(2000);
    for (int i = 0; i < 2000; ++i) contexts.push_back(d.next());

    long allocs = 0;
    {
        AllocationCounter counter;
        for (auto& m : motions) {
            for (const auto& c : contexts) m->evaluate(c);
            m->reset();
            for (const auto& c : contexts) m->evaluate(c);
        }
        allocs = counter.count();
    }
    CHECK(allocs == 0);
}

TEST_CASE("Non-finite modulation holds the previous position") {
    OrbitTrajectory o(simpleOrbit());
    ClockDriver d;
    const Position before = run(o, d, 10);
    MotionModulation m;
    m[MotionParam::CentreAz] = std::nanf("");
    o.setModulation(m);
    CHECK(o.evaluate(d.next()) == before);
}
