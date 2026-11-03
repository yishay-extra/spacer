#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <set>

#include "core/LayoutPresets.h"
#include "core/Validate.h"
#include "core/motion/MotionParamValue.h"
#include "ui/logic/MotionFields.h"

using namespace spacer;
using Catch::Matchers::WithinAbs;

namespace {

Layout quad() {
    return *layoutpresets::load("quad");
}

const MotionField* field(const std::vector<MotionField>& fields, const std::string& id) {
    for (const auto& f : fields)
        if (f.id == id) return &f;
    return nullptr;
}

std::set<std::string> ids(const MotionSpec& m) {
    std::set<std::string> out;
    for (const auto& f : motionFields(m)) out.insert(f.id);
    return out;
}

}  // namespace

TEST_CASE("Every kind has a field table with unique ids", "[ui][motion]") {
    for (auto kind : allMotionKinds()) {
        const auto m = makeMotion(kind);
        REQUIRE(motionKindOf(m) == kind);
        const auto fields = motionFields(m);
        REQUIRE_FALSE(fields.empty());
        std::set<std::string> seen;
        for (const auto& f : fields) {
            REQUIRE_FALSE(f.id.empty());
            REQUIRE_FALSE(f.label.empty());
            REQUIRE(seen.insert(f.id).second);  // unique
            REQUIRE_FALSE(f.text.empty());
            if (f.type == FieldType::Choice) REQUIRE(f.choice >= 0);
        }
        REQUIRE(std::string(motionKindName(kind)) != "");
    }
}

TEST_CASE("Default motions validate", "[ui][motion]") {
    const Layout layout = quad();
    for (auto kind : allMotionKinds()) {
        SourceSpec s;
        s.id = 1;
        s.motion = makeMotion(kind);
        REQUIRE_FALSE(hasErrors(validate(s, layout)));
    }
}

TEST_CASE("Tables list the parameters of each kind", "[ui][motion]") {
    REQUIRE(ids(makeMotion(MotionKind::Manual)) ==
            std::set<std::string>{"position.az", "position.el", "position.dist", "smoothMs"});
    const auto orbit = ids(makeMotion(MotionKind::Orbit));
    for (const char* id : {"centre.az", "centre.el", "centre.dist", "radius", "period", "direction",
                           "phaseOffset", "elevationWobble", "wobblePeriod"})
        REQUIRE(orbit.count(id));
    const auto line = ids(makeMotion(MotionKind::Line));
    for (const char* id : {"from.az", "to.dist", "period", "mode", "easing"}) REQUIRE(line.count(id));
    const auto liss = ids(makeMotion(MotionKind::Lissajous));
    for (const char* id : {"ax", "ay", "az", "fx", "fy", "fz", "phases.x", "phases.y", "phases.z", "period"})
        REQUIRE(liss.count(id));
    const auto path = ids(makeMotion(MotionKind::Path));
    for (const char* id : {"period", "mode", "interpolation", "constantSpeed", "points.0.x", "points.1.z"})
        REQUIRE(path.count(id));
    const auto walk = ids(makeMotion(MotionKind::RandomWalk));
    for (const char* id :
         {"stepMetres", "stepPeriod", "glide", "seed", "bounds", "sphere.centre.x", "sphere.radius"})
        REQUIRE(walk.count(id));
    const auto steps = ids(makeMotion(MotionKind::StepSequence));
    for (const char* id : {"order", "loop", "seed", "steps.0.target", "steps.0.az", "steps.1.duration",
                           "steps.1.transition", "steps.1.probability"})
        REQUIRE(steps.count(id));
}

TEST_CASE("Field values reflect the spec", "[ui][motion]") {
    OrbitMotion o;
    o.radius = 2.5f;
    o.period = Period::seconds(7.5);
    o.direction = -1;
    const auto fields = motionFields(MotionSpec{o});
    REQUIRE(field(fields, "radius")->value == 2.5f);
    REQUIRE(field(fields, "radius")->text == "2.5 m");
    REQUIRE(field(fields, "period")->period == Period::seconds(7.5));
    REQUIRE(field(fields, "period")->text == "7.5 s");
    REQUIRE(field(fields, "direction")->choice == 1);
    REQUIRE(field(fields, "direction")->text == "Counter-clockwise");
}

TEST_CASE("Typing a value edits the spec", "[ui][motion]") {
    const Layout layout = quad();
    MotionSpec m = makeMotion(MotionKind::Orbit);
    REQUIRE(setMotionField(m, layout, "radius", "3.5 m"));
    REQUIRE(std::get<OrbitMotion>(m.value).radius == 3.5f);
    REQUIRE(setMotionField(m, layout, "period", "2.5s"));
    REQUIRE(std::get<OrbitMotion>(m.value).period == Period::seconds(2.5));
    REQUIRE(setMotionField(m, layout, "direction", "Counter-clockwise"));
    REQUIRE(std::get<OrbitMotion>(m.value).direction == -1);
    REQUIRE(setMotionField(m, layout, "direction", "0"));  // by index
    REQUIRE(std::get<OrbitMotion>(m.value).direction == 1);
    REQUIRE(setMotionField(m, layout, "centre.az", "45"));
    REQUIRE(std::get<OrbitMotion>(m.value).centre.azimuth == 45.0f);
    REQUIRE(setMotionField(m, layout, "phaseOffset", "90\xC2\xB0"));
    REQUIRE(std::get<OrbitMotion>(m.value).phaseOffset == 90.0f);
}

TEST_CASE("Numbers are clamped and invalid text changes nothing", "[ui][motion]") {
    const Layout layout = quad();
    MotionSpec m = makeMotion(MotionKind::Orbit);
    REQUIRE(setMotionField(m, layout, "centre.el", "400"));
    REQUIRE(std::get<OrbitMotion>(m.value).centre.elevation == 90.0f);  // validate(): within [-90, 90]
    REQUIRE(setMotionField(m, layout, "radius", "-3"));
    REQUIRE(std::get<OrbitMotion>(m.value).radius == 0.0f);

    const MotionSpec before = m;
    REQUIRE_FALSE(setMotionField(m, layout, "radius", "wide"));
    REQUIRE_FALSE(setMotionField(m, layout, "period", "0"));
    REQUIRE_FALSE(setMotionField(m, layout, "direction", "sideways"));
    REQUIRE_FALSE(setMotionField(m, layout, "direction", "7"));
    REQUIRE_FALSE(setMotionField(m, layout, "no.such.field", "1"));
    REQUIRE(m == before);
}

TEST_CASE("Edited motions still validate", "[ui][motion]") {
    const Layout layout = quad();
    for (auto kind : allMotionKinds()) {
        MotionSpec m = makeMotion(kind);
        // Throw extreme values at every field.
        for (const char* text : {"-1000", "1000", "0", "9.99", "1"})
            for (const auto& f : motionFields(m)) {
                if (f.type == FieldType::Number || f.type == FieldType::Period)
                    setMotionField(m, layout, f.id, text);
            }
        SourceSpec s;
        s.id = 1;
        s.motion = m;
        INFO(motionKindName(kind));
        REQUIRE_FALSE(hasErrors(validate(s, layout)));
    }
}

TEST_CASE("Live parameters are the continuous ones of moving kinds", "[ui][motion]") {
    const MotionSpec orbit = makeMotion(MotionKind::Orbit);
    REQUIRE(liveParamOf(orbit, "radius") == MotionParam::Radius);
    REQUIRE(liveParamOf(orbit, "centre.az") == MotionParam::CentreAz);
    REQUIRE(liveParamOf(orbit, "phaseOffset") == MotionParam::PhaseOffset);
    // Periods, direction and everything else restart the phase / rebuild (SPEC §8).
    REQUIRE_FALSE(liveParamOf(orbit, "period"));
    REQUIRE_FALSE(liveParamOf(orbit, "direction"));
    REQUIRE_FALSE(liveParamOf(orbit, "wobblePeriod"));
    REQUIRE(liveParamOf(makeMotion(MotionKind::Line), "to.dist") == MotionParam::ToDist);
    REQUIRE(liveParamOf(makeMotion(MotionKind::Lissajous), "phases.z") == MotionParam::PhaseZ);
    REQUIRE_FALSE(liveParamOf(makeMotion(MotionKind::Lissajous), "fx"));
    REQUIRE(liveParamOf(makeMotion(MotionKind::RandomWalk), "stepMetres") == MotionParam::StepMetres);
    REQUIRE_FALSE(liveParamOf(makeMotion(MotionKind::Path), "points.0.x"));
    REQUIRE_FALSE(liveParamOf(makeMotion(MotionKind::StepSequence), "steps.0.az"));
    // Manual positions travel as SetSourcePosition instead.
    REQUIRE_FALSE(liveParamOf(makeMotion(MotionKind::Manual), "position.az"));
}

TEST_CASE("Every live field's parameter reads back through motionParamValue", "[ui][motion]") {
    for (auto kind : allMotionKinds()) {
        const MotionSpec m = makeMotion(kind);
        for (const auto& f : motionFields(m)) {
            if (!f.param) continue;
            INFO(f.id);
            const auto v = motionParamValue(m, *f.param);
            REQUIRE(v);
            REQUIRE_THAT(*v, WithinAbs(f.value, 1e-5));
        }
    }
}

TEST_CASE("setMotionParamValue is the inverse of motionParamValue", "[ui][motion]") {
    MotionSpec m = makeMotion(MotionKind::Orbit);
    REQUIRE(setMotionParamValue(m, MotionParam::Radius, 4.25f));
    REQUIRE(motionParamValue(m, MotionParam::Radius) == 4.25f);
    REQUIRE_FALSE(setMotionParamValue(m, MotionParam::Ax, 1.0f));  // not an Orbit parameter
    REQUIRE_FALSE(setMotionParamValue(m, MotionParam::Radius, std::nanf("")));
}

TEST_CASE("Path points can be added and removed down to two", "[ui][motion]") {
    MotionSpec m = makeMotion(MotionKind::Path);
    REQUIRE(motionItemName(m) == "Point");
    REQUIRE(motionItemCount(m) == 2);
    REQUIRE(addMotionItem(m));
    REQUIRE(motionItemCount(m) == 3);
    REQUIRE(std::get<PathMotion>(m.value).points[2].x == std::get<PathMotion>(m.value).points[1].x + 0.5f);
    REQUIRE(ids(m).count("points.2.y"));
    REQUIRE(removeMotionItem(m, 0));
    REQUIRE(motionItemCount(m) == 2);
    REQUIRE_FALSE(removeMotionItem(m, 0));  // a path needs 2 points
    REQUIRE_FALSE(removeMotionItem(m, 5));
    REQUIRE_FALSE(removeMotionItem(m, -1));
}

TEST_CASE("Steps can be added and removed down to one", "[ui][motion]") {
    const Layout layout = quad();
    MotionSpec m = makeMotion(MotionKind::StepSequence);
    REQUIRE(motionItemName(m) == "Step");
    REQUIRE(addMotionItem(m));
    REQUIRE(motionItemCount(m) == 3);
    REQUIRE(removeMotionItem(m, 2));
    REQUIRE(removeMotionItem(m, 0));
    REQUIRE_FALSE(removeMotionItem(m, 0));  // a sequence needs one step
    REQUIRE(motionItemCount(m) == 1);

    MotionSpec manual = makeMotion(MotionKind::Manual);
    REQUIRE(motionItemName(manual).empty());
    REQUIRE_FALSE(addMotionItem(manual));
    REQUIRE_FALSE(removeMotionItem(manual, 0));
}

TEST_CASE("A step target switches between a position and a speaker", "[ui][motion]") {
    const Layout layout = quad();
    MotionSpec m = makeMotion(MotionKind::StepSequence);
    REQUIRE(setMotionField(m, layout, "steps.0.target", "Speaker"));
    const auto& step = std::get<StepSequenceMotion>(m.value).steps[0];
    REQUIRE(std::get<int>(step.target) == layout.speakers.front().id);
    REQUIRE(ids(m).count("steps.0.speaker"));
    REQUIRE_FALSE(ids(m).count("steps.0.az"));
    REQUIRE(setMotionField(m, layout, "steps.0.speaker", "3"));
    REQUIRE(std::get<int>(std::get<StepSequenceMotion>(m.value).steps[0].target) == 3);
    // Back to a position: lands on that speaker.
    REQUIRE(setMotionField(m, layout, "steps.0.target", "Position"));
    const auto& pos = std::get<Position>(std::get<StepSequenceMotion>(m.value).steps[0].target);
    REQUIRE(pos == layout.findSpeaker(3)->position);
    // Glide time only exists for glide steps.
    REQUIRE_FALSE(ids(m).count("steps.0.glideTime"));
    REQUIRE(setMotionField(m, layout, "steps.0.transition", "Glide"));
    REQUIRE(ids(m).count("steps.0.glideTime"));
    REQUIRE(setMotionField(m, layout, "steps.0.probability", "50%"));
    REQUIRE(std::get<StepSequenceMotion>(m.value).steps[0].probability == 0.5f);
}

TEST_CASE("Random-walk bounds switch shape keeping the volume", "[ui][motion]") {
    const Layout layout = quad();
    MotionSpec m = makeMotion(MotionKind::RandomWalk);
    REQUIRE(setMotionField(m, layout, "bounds", "Box"));
    const auto box = std::get<BoxBounds>(std::get<RandomWalkMotion>(m.value).bounds);
    REQUIRE(box.min.x == -2.0f);
    REQUIRE(box.max.x == 2.0f);
    REQUIRE(ids(m).count("box.min.x"));
    REQUIRE(setMotionField(m, layout, "box.max.z", "5"));
    REQUIRE(setMotionField(m, layout, "bounds", "Sphere"));
    REQUIRE(std::get<SphereBounds>(std::get<RandomWalkMotion>(m.value).bounds).radius > 0.0f);
}

TEST_CASE("Items carry their index for per-row buttons", "[ui][motion]") {
    const auto fields = motionFields(makeMotion(MotionKind::Path));
    REQUIRE(field(fields, "points.1.y")->item == 1);
    REQUIRE(field(fields, "period")->item == -1);
    REQUIRE(field(fields, "points.0.x")->group == "Point 1");
}
