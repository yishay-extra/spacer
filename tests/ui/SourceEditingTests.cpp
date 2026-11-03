#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/LayoutPresets.h"
#include "core/Validate.h"
#include "core/motion/ParameterPath.h"
#include "ui/logic/EngineSync.h"
#include "ui/logic/ProjectHistory.h"
#include "ui/logic/SourceEditing.h"

using namespace spacer;
using namespace spacer::sourceedit;
using Kind = EngineEffect::Kind;

namespace {

Project makeProject() {
    Project p;
    p.layout = *layoutpresets::load("quad");
    p.sources.push_back(makeSource(p, SourceKind::VirtualInput));
    p.sources.push_back(makeSource(p, SourceKind::Generator));
    p.sources.back().id = 2;
    p.sources.back().name = "Tone";
    OrbitMotion orbit;
    orbit.radius = 2.0f;
    p.sources.back().motion.value = orbit;
    return p;
}

const MotionField* find(const std::vector<MotionField>& fields, const std::string& id) {
    for (const auto& f : fields)
        if (f.id == id) return &f;
    return nullptr;
}

bool valid(const Project& p) {
    return !hasErrors(validate(p));
}

template <typename T>
const T* commandOf(const EngineSyncPlan& plan) {
    for (const auto& c : plan.commands)
        if (const auto* t = std::get_if<T>(&c)) return t;
    return nullptr;
}

const SourceSpec& source(const Project& p, int id) {
    return *std::find_if(p.sources.begin(), p.sources.end(),
                         [id](const SourceSpec& s) { return s.id == id; });
}

}  // namespace

// ---- add / remove / duplicate ---------------------------------------------------------

TEST_CASE("New sources of every kind are valid and unique", "[ui][sources]") {
    Project p = makeProject();
    for (auto kind :
         {SourceKind::VirtualInput, SourceKind::HardwareInput, SourceKind::File, SourceKind::Generator}) {
        SourceSpec s = makeSource(p, kind);
        REQUIRE(s.kind() == kind);
        REQUIRE(s.id == nextSourceId(p));
        REQUIRE(std::none_of(p.sources.begin(), p.sources.end(),
                             [&s](const SourceSpec& o) { return o.name == s.name; }));
        p.sources.push_back(s);
    }
    // A file source has no file yet: validate() flags exactly that, nothing else.
    const auto issues = validate(p);
    REQUIRE(std::count_if(issues.begin(), issues.end(),
                          [](const Issue& i) { return i.severity == Issue::Severity::Error; }) == 1);
}

TEST_CASE("New virtual inputs take the lowest unused channel", "[ui][sources]") {
    Project p;
    p.layout = *layoutpresets::load("stereo");
    for (int i = 0; i < 3; ++i) p.sources.push_back(makeSource(p, SourceKind::VirtualInput));
    REQUIRE(std::get<VirtualInputSpec>(p.sources[0].input.value).left == 0);
    REQUIRE(std::get<VirtualInputSpec>(p.sources[1].input.value).left == 1);
    REQUIRE(std::get<VirtualInputSpec>(p.sources[2].input.value).left == 2);
    p.sources.erase(p.sources.begin() + 1);
    REQUIRE(std::get<VirtualInputSpec>(makeSource(p, SourceKind::VirtualInput).input.value).left == 1);
}

TEST_CASE("Adding, duplicating and removing are undoable", "[ui][sources]") {
    Project p = makeProject();
    const Project original = p;
    ProjectHistory h;
    int newId = 0;
    REQUIRE(h.perform(p, addSource(p, SourceKind::Generator, &newId)));
    REQUIRE(newId == 3);
    REQUIRE(p.sources.size() == 3);
    REQUIRE(p.sources.back().id == 3);

    int dupId = 0;
    const auto effect = h.perform(p, duplicateSource(p, 1, &dupId));
    REQUIRE(effect);
    REQUIRE(effect->kind == Kind::Structural);
    REQUIRE(dupId == 4);
    REQUIRE(p.sources[1].id == 4);  // right after the original
    REQUIRE(p.sources[1].name == p.sources[0].name + " copy");
    REQUIRE(p.sources[1].input == p.sources[0].input);
    REQUIRE(valid(p));

    REQUIRE(h.perform(p, removeSource(p, 2)));
    REQUIRE(p.sources.size() == 3);
    REQUIRE_FALSE(
        std::any_of(p.sources.begin(), p.sources.end(), [](const SourceSpec& s) { return s.id == 2; }));

    // Undo everything: the original list in the original order.
    h.undo(p);
    REQUIRE(p.sources.size() == 4);
    REQUIRE(p.sources[2].id == 2);  // back in its place
    h.undo(p);
    h.undo(p);
    REQUIRE(p == original);
    h.redo(p);
    h.redo(p);
    h.redo(p);
    REQUIRE(p.sources.size() == 3);
}

TEST_CASE("Duplicates get distinct names and the source limit holds", "[ui][sources]") {
    Project p = makeProject();
    for (int i = 0; i < 2; ++i) {
        auto edit = duplicateSource(p, 1);
        REQUIRE(edits::isValid(edit));
        edit.apply(p);
    }
    REQUIRE(p.sources[1].name != p.sources[2].name);

    while (static_cast<int>(p.sources.size()) < kMaxSources) {
        auto edit = addSource(p, SourceKind::Generator);
        REQUIRE(edits::isValid(edit));
        edit.apply(p);
    }
    REQUIRE_FALSE(edits::isValid(addSource(p, SourceKind::Generator)));
    REQUIRE_FALSE(edits::isValid(duplicateSource(p, 1)));
    REQUIRE(edits::isValid(removeSource(p, 1)));
    REQUIRE_FALSE(edits::isValid(removeSource(p, 999)));
    REQUIRE_FALSE(edits::isValid(duplicateSource(p, 999)));
}

// ---- input / level / spatializer fields ------------------------------------------------

TEST_CASE("Level fields edit name, mute and gain", "[ui][sources]") {
    Project p = makeProject();
    SourceSpec& s = p.sources[0];
    REQUIRE(setSourceField(s, p.layout, "name", "  Voice "));
    REQUIRE(s.name == "Voice");
    REQUIRE(setSourceField(s, p.layout, "mute", "on"));
    REQUIRE(s.mute);
    REQUIRE(setSourceField(s, p.layout, "gainDb", "-6 dB"));
    REQUIRE(s.gainDb == -6.0f);
    REQUIRE(setSourceField(s, p.layout, "gainDb", "40"));
    REQUIRE(s.gainDb == kGainMaxDb);
    REQUIRE(setSourceField(s, p.layout, "gainDb", "-inf"));
    REQUIRE(s.gainDb == kGainMinDb);
    REQUIRE_FALSE(setSourceField(s, p.layout, "gainDb", "loud"));
    REQUIRE(valid(p));

    const auto level = sourceFields(s, p.layout, SourceSection::Level);
    REQUIRE(find(level, "name")->type == FieldType::Text);
    REQUIRE(find(level, "gainDb")->unit == fmt::Unit::Db);
}

TEST_CASE("Spread and distance edit as percent and plain numbers", "[ui][sources]") {
    Project p = makeProject();
    SourceSpec& s = p.sources[0];
    REQUIRE(setSourceField(s, p.layout, "spread", "40%"));
    REQUIRE(s.spread == 0.4f);
    REQUIRE(setSourceField(s, p.layout, "spread", "400"));
    REQUIRE(s.spread == 1.0f);
    REQUIRE(setSourceField(s, p.layout, "distance.k", "2"));
    REQUIRE(setSourceField(s, p.layout, "distance.rMin", "0"));
    REQUIRE(s.distance.k == 2.0f);
    REQUIRE(s.distance.rMin == 0.1f);  // rMin > 0 (validate)
    REQUIRE(setSourceField(s, p.layout, "distance.q", "1.5"));
    REQUIRE(s.distance.q == 1.5f);
    REQUIRE(valid(p));
}

TEST_CASE("Stereo repairs channels and linking", "[ui][sources]") {
    Project p = makeProject();
    SourceSpec& s = p.sources[0];
    std::get<VirtualInputSpec>(s.input.value).left = 4;
    REQUIRE(setSourceField(s, p.layout, "stereo", "on"));
    REQUIRE(std::get<VirtualInputSpec>(s.input.value).right == 5);
    REQUIRE(valid(p));
    REQUIRE(find(sourceFields(s, p.layout, SourceSection::Input), "input.right"));
    REQUIRE(find(sourceFields(s, p.layout, SourceSection::Spatial), "width"));

    REQUIRE(setSourceField(s, p.layout, "linkedPoints", "on"));
    REQUIRE(s.linkedPoints);
    REQUIRE(s.motionRight == s.motion);
    REQUIRE(valid(p));
    REQUIRE(motionChannelCount(s) == 2);

    REQUIRE(setSourceField(s, p.layout, "stereo", "off"));
    REQUIRE_FALSE(s.linkedPoints);
    REQUIRE_FALSE(s.motionRight);
    REQUIRE(std::get<VirtualInputSpec>(s.input.value).right == -1);
    REQUIRE(valid(p));
    REQUIRE(motionChannelCount(s) == 1);
    REQUIRE_FALSE(find(sourceFields(s, p.layout, SourceSection::Spatial), "width"));
}

TEST_CASE("The last virtual channel cannot overflow when going stereo", "[ui][sources]") {
    Project p = makeProject();
    std::get<VirtualInputSpec>(p.sources[0].input.value).left = 63;
    REQUIRE(setSourceField(p.sources[0], p.layout, "stereo", "on"));
    REQUIRE(std::get<VirtualInputSpec>(p.sources[0].input.value).right == 63);
    REQUIRE(valid(p));
}

TEST_CASE("Generators are mono", "[ui][sources]") {
    Project p = makeProject();
    SourceSpec& tone = p.sources[1];
    REQUIRE_FALSE(setSourceField(tone, p.layout, "stereo", "on"));
    REQUIRE_FALSE(find(sourceFields(tone, p.layout, SourceSection::Input), "stereo"));
    // A stereo / linked source that becomes a generator turns mono.
    SourceSpec s = p.sources[0];
    REQUIRE(setSourceField(s, p.layout, "stereo", "on"));
    REQUIRE(setSourceField(s, p.layout, "linkedPoints", "on"));
    REQUIRE(setSourceField(s, p.layout, "input.kind", "Generator"));
    REQUIRE_FALSE(s.stereo);
    REQUIRE_FALSE(s.linkedPoints);
    REQUIRE_FALSE(s.motionRight);
    p.sources[0] = s;
    REQUIRE(valid(p));
}

TEST_CASE("Channel boxes are 1-based", "[ui][sources]") {
    Project p = makeProject();
    SourceSpec& s = p.sources[0];
    const auto fields = sourceFields(s, p.layout, SourceSection::Input);
    REQUIRE(find(fields, "input.left")->value == 1.0f);  // model channel 0
    REQUIRE(setSourceField(s, p.layout, "input.left", "10"));
    REQUIRE(std::get<VirtualInputSpec>(s.input.value).left == 9);
    REQUIRE(setSourceField(s, p.layout, "input.left", "500"));
    REQUIRE(std::get<VirtualInputSpec>(s.input.value).left == 63);
    REQUIRE(setSourceField(s, p.layout, "input.kind", "Hardware input"));
    REQUIRE(setSourceField(s, p.layout, "input.left", "500"));
    REQUIRE(std::get<HardwareInputSpec>(s.input.value).left == 499);
}

TEST_CASE("Switching the input kind keeps the source valid", "[ui][sources]") {
    Project p = makeProject();
    SourceSpec& s = p.sources[0];
    REQUIRE(setSourceField(s, p.layout, "input.kind", "File"));
    REQUIRE(s.kind() == SourceKind::File);
    REQUIRE(setSourceField(s, p.layout, "input.path", "/tmp/a.wav"));
    REQUIRE(setSourceField(s, p.layout, "input.loop", "1"));
    REQUIRE(setSourceField(s, p.layout, "input.startSeconds", "1.5 s"));
    REQUIRE(setSourceField(s, p.layout, "input.gainDb", "-3"));
    REQUIRE(setSourceField(s, p.layout, "input.playOnSceneRecall", "yes"));
    const auto& f = std::get<FileInputSpec>(s.input.value);
    REQUIRE(f.path == "/tmp/a.wav");
    REQUIRE(f.loop);
    REQUIRE(f.startSeconds == 1.5f);
    REQUIRE(f.gainDb == -3.0f);
    REQUIRE(f.playOnSceneRecall);
    REQUIRE(valid(p));

    REQUIRE(setSourceField(s, p.layout, "input.kind", "Generator"));
    REQUIRE(setSourceField(s, p.layout, "input.generator", "Impulse"));
    REQUIRE(setSourceField(s, p.layout, "input.periodSeconds", "0.5"));
    REQUIRE(std::get<GeneratorSpec>(s.input.value).periodSeconds == 0.5f);
    REQUIRE(valid(p));
    const auto fields = sourceFields(s, p.layout, SourceSection::Input);
    REQUIRE(find(fields, "input.periodSeconds"));
    REQUIRE_FALSE(find(fields, "input.hz"));

    REQUIRE(setSourceField(s, p.layout, "input.generator", "Sine"));
    REQUIRE(setSourceField(s, p.layout, "input.hz", "1000"));
    REQUIRE(std::get<GeneratorSpec>(s.input.value).hz == 1000.0f);
    REQUIRE(valid(p));
}

TEST_CASE("Direct needs a speaker and keeps at least one", "[ui][sources]") {
    Project p = makeProject();
    SourceSpec& s = p.sources[0];
    REQUIRE(setSourceField(s, p.layout, "spatializer", "Direct"));
    REQUIRE(s.directSpeakers.size() == 1);
    REQUIRE(valid(p));
    const auto fields = sourceFields(s, p.layout, SourceSection::Spatial);
    REQUIRE(find(fields, "direct." + std::to_string(p.layout.speakers[0].id)));
    REQUIRE_FALSE(find(fields, "spread"));  // Direct ignores spread, distance and position
    REQUIRE_FALSE(find(fields, "distance.k"));

    const int second = p.layout.speakers[1].id;
    REQUIRE(setSourceField(s, p.layout, "direct." + std::to_string(second), "on"));
    REQUIRE(s.directSpeakers.size() == 2);
    REQUIRE(setSourceField(s, p.layout, "direct." + std::to_string(second), "off"));
    REQUIRE_FALSE(setSourceField(s, p.layout, "direct." + std::to_string(p.layout.speakers[0].id), "off"));
    REQUIRE(s.directSpeakers.size() == 1);

    REQUIRE(setSourceField(s, p.layout, "spatializer", "DBAP"));
    REQUIRE(find(sourceFields(s, p.layout, SourceSection::Spatial), "spread"));
}

// ---- modulators ----------------------------------------------------------------------------

TEST_CASE("Modulators: up to four, with sensible targets", "[ui][sources]") {
    Project p = makeProject();
    SourceSpec& s = p.sources[1];  // Orbit
    for (int i = 0; i < kMaxModulators; ++i) REQUIRE(addModulator(s));
    REQUIRE_FALSE(addModulator(s));
    REQUIRE(valid(p));

    const auto targets = modulatorTargets(s);
    for (const char* t :
         {"gain", "spread", "distance.k", "motion.radius", "motion.centre.az", "motion.phaseOffset"})
        REQUIRE(std::find(targets.begin(), targets.end(), t) != targets.end());
    REQUIRE(std::find(targets.begin(), targets.end(), "width") == targets.end());  // mono
    REQUIRE(std::find(targets.begin(), targets.end(), "motion.ax") ==
            targets.end());  // not an Orbit parameter
    for (const auto& t : targets) REQUIRE(ParameterPath::parse(t));

    REQUIRE(removeModulator(s, 1));
    REQUIRE_FALSE(removeModulator(s, 3));
    REQUIRE_FALSE(removeModulator(s, -1));
    REQUIRE(s.modulators.size() == 3);
}

TEST_CASE("Modulator fields edit source, target, depth and offset", "[ui][sources]") {
    Project p = makeProject();
    SourceSpec& s = p.sources[1];
    REQUIRE(addModulator(s));
    REQUIRE(setSourceField(s, p.layout, "mod.0.shape", "Triangle"));
    REQUIRE(setSourceField(s, p.layout, "mod.0.period", "2 s"));
    REQUIRE(setSourceField(s, p.layout, "mod.0.target", "motion.radius"));
    REQUIRE(setSourceField(s, p.layout, "mod.0.depth", "0.5"));
    REQUIRE(setSourceField(s, p.layout, "mod.0.offset", "-1"));
    const auto& lfo = std::get<LfoSource>(s.modulators[0].source);
    REQUIRE(lfo.shape == LfoShape::Triangle);
    REQUIRE(lfo.period == Period::seconds(2));
    REQUIRE(s.modulators[0].target == "motion.radius");
    REQUIRE(s.modulators[0].depth == 0.5f);
    REQUIRE(s.modulators[0].offset == -1.0f);
    REQUIRE(valid(p));

    REQUIRE(setSourceField(s, p.layout, "mod.0.source", "OSC"));
    REQUIRE_FALSE(setSourceField(s, p.layout, "mod.0.address", "no-slash"));
    REQUIRE(setSourceField(s, p.layout, "mod.0.address", "/lfo/1"));
    REQUIRE(std::get<OscSource>(s.modulators[0].source).address == "/lfo/1");
    REQUIRE(valid(p));
    REQUIRE(setSourceField(s, p.layout, "mod.0.source", "MIDI CC"));
    REQUIRE(setSourceField(s, p.layout, "mod.0.cc", "200"));
    REQUIRE(std::get<MidiCcSource>(s.modulators[0].source).cc == 127);
    REQUIRE(valid(p));
    REQUIRE_FALSE(setSourceField(s, p.layout, "mod.0.target", "(none)"));
    REQUIRE_FALSE(setSourceField(s, p.layout, "mod.0.target", "motion.nope"));
}

TEST_CASE("A modulator with a stale target still shows it", "[ui][sources]") {
    Project p = makeProject();
    SourceSpec s = p.sources[0];  // Manual
    ModulatorSpec m;
    m.target = "motion.radius";  // not a Manual parameter
    s.modulators.push_back(m);
    const auto fields =
        sourceFields(s, p.layout, SourceSection::Modulators);  // keep alive: find() points into it
    const auto* target = find(fields, "mod.0.target");
    REQUIRE(target);
    REQUIRE(target->text == "motion.radius");
}

TEST_CASE("Insert rows show a readable name and the bypass flag", "[ui][sources]") {
    SourceSpec s;
    REQUIRE(insertRows(s).empty());
    PluginSlot slot;
    slot.pluginId = "AudioUnit:Effects/aufx,dely,appl";
    slot.bypassed = true;
    s.inserts = {slot, PluginSlot{}};
    const auto rows = insertRows(s);
    REQUIRE(rows.size() == 2);
    REQUIRE(rows[0].name == "aufx,dely,appl");
    REQUIRE(rows[0].bypassed);
    REQUIRE(rows[1].name == "(empty)");
}

TEST_CASE("Inserts: add up to four, remove, bypass without a rebuild", "[ui][sources]") {
    Project p = makeProject();
    ProjectHistory h;
    for (int i = 0; i < kMaxInserts; ++i) {
        auto edit = editAddInsert(p, 1, "AudioUnit:Effects/aufx,dely,appl");
        REQUIRE(edit);
        REQUIRE(edit->effect.kind == Kind::Structural);
        h.perform(p, *edit);
    }
    REQUIRE_FALSE(editAddInsert(p, 1, "AudioUnit:Effects/aufx,dely,appl"));  // SPEC §1: 4 per source
    REQUIRE_FALSE(editAddInsert(p, 1, ""));
    REQUIRE(valid(p));

    const auto bypass = editInsertBypass(p, 1, 2, true);
    REQUIRE(bypass);
    REQUIRE(bypass->effect.kind == Kind::InsertBypass);
    const auto effect = h.perform(p, *bypass);
    REQUIRE(p.sources[0].inserts[2].bypassed);
    auto plan = planEngineSync(p, *effect);
    REQUIRE_FALSE(plan.rebuildGraph);
    const auto* c = commandOf<cmd::SetInsertBypass>(plan);
    REQUIRE(c);
    REQUIRE(c->owner == InsertOwner::Source);
    REQUIRE(c->id == 1);
    REQUIRE(c->slot == 2);
    REQUIRE(c->bypassed);
    REQUIRE_FALSE(editInsertBypass(p, 1, 2, true));  // unchanged
    REQUIRE_FALSE(editInsertBypass(p, 1, 9, true));

    // Undo sends the old flag through the same path.
    plan = planEngineSync(p, *h.undo(p));
    REQUIRE_FALSE(commandOf<cmd::SetInsertBypass>(plan)->bypassed);

    REQUIRE(h.perform(p, *editRemoveInsert(p, 1, 0)));
    REQUIRE(p.sources[0].inserts.size() == 3);
    REQUIRE_FALSE(editRemoveInsert(p, 1, 3));
    h.undo(p);
    REQUIRE(p.sources[0].inserts.size() == 4);
}

TEST_CASE("A bypass effect for a vanished slot rebuilds", "[ui][sources]") {
    Project p = makeProject();
    REQUIRE(planEngineSync(p, EngineEffect::insertBypass(1, 0)).rebuildGraph);
    REQUIRE(planEngineSync(p, EngineEffect::insertBypass(99, 0)).rebuildGraph);
}

// ---- edits and engine routing --------------------------------------------------------------

TEST_CASE("Field edits are undoable and skip no-ops", "[ui][sources]") {
    Project p = makeProject();
    ProjectHistory h;
    auto edit = editSourceField(p, 1, "gainDb", "-6");
    REQUIRE(edit);
    REQUIRE(edit->name == "Set Gain");
    REQUIRE(h.perform(p, *edit));
    REQUIRE(p.sources[0].gainDb == -6.0f);
    h.undo(p);
    REQUIRE(p.sources[0].gainDb == 0.0f);
    h.redo(p);
    REQUIRE(p.sources[0].gainDb == -6.0f);

    REQUIRE_FALSE(editSourceField(p, 1, "gainDb", "-6"));  // unchanged: no empty undo step
    REQUIRE_FALSE(editSourceField(p, 1, "gainDb", "nope"));
    REQUIRE_FALSE(editSourceField(p, 99, "gainDb", "-3"));
    REQUIRE_FALSE(editSourceField(p, 1, "nonsense", "-3"));
}

TEST_CASE("Level edits of a moving source do not rebuild the graph", "[ui][sources]") {
    Project p = makeProject();
    for (const char* field : {"gainDb", "mute", "spread", "distance.k"}) {
        const std::string text = std::string(field) == "mute" ? "on" : "0.5";
        auto edit = editSourceField(p, 2, field, text);  // source 2 orbits
        INFO(field);
        REQUIRE(edit);
        REQUIRE(edit->effect.kind == Kind::Source);
        Project after = p;
        edit->apply(after);
        const auto plan = planEngineSync(after, edit->effect);
        REQUIRE_FALSE(plan.rebuildGraph);
        REQUIRE(commandOf<cmd::SetSourcePosition>(plan) == nullptr);
    }
}

TEST_CASE("Structural source edits rebuild", "[ui][sources]") {
    const Project p = makeProject();
    for (auto [field, text] :
         {std::pair{"stereo", "on"}, {"spatializer", "DBAP"}, {"input.kind", "Hardware input"}}) {
        INFO(field);
        const auto edit = editSourceField(p, 1, field, text);
        REQUIRE(edit);
        REQUIRE(edit->effect.kind == Kind::Structural);
    }
    REQUIRE(editAddModulator(p, 1)->effect.kind == Kind::Structural);
}

TEST_CASE("A continuous motion parameter is a live SetMotionParam edit", "[ui][sources]") {
    Project p = makeProject();
    auto edit = editMotionField(p, 2, 0, "radius", "3.5");
    REQUIRE(edit);
    REQUIRE(edit->effect.kind == Kind::MotionParam);
    REQUIRE(edit->effect.id == 2);
    REQUIRE(edit->effect.channel == -1);
    REQUIRE(edit->effect.param == MotionParam::Radius);

    ProjectHistory h;
    const auto effect = h.perform(p, *edit);
    REQUIRE(effect);
    auto plan = planEngineSync(p, *effect);
    REQUIRE_FALSE(plan.rebuildGraph);
    REQUIRE(commandOf<cmd::SetMotionParam>(plan)->value == 3.5f);

    // Undo goes through the same path and sends the old value.
    const auto undone = h.undo(p);
    REQUIRE(undone);
    plan = planEngineSync(p, *undone);
    REQUIRE(commandOf<cmd::SetMotionParam>(plan)->value == 2.0f);
}

TEST_CASE("Period and other structural motion fields rebuild", "[ui][sources]") {
    const Project p = makeProject();
    for (auto [field, text] :
         {std::pair{"period", "4"}, {"direction", "Counter-clockwise"}, {"wobblePeriod", "3 s"}}) {
        INFO(field);
        const auto edit = editMotionField(p, 2, 0, field, text);
        REQUIRE(edit);
        REQUIRE(edit->effect.kind == Kind::Structural);
    }
}

TEST_CASE("Manual positions keep using SetSourcePosition", "[ui][sources]") {
    Project p = makeProject();
    const auto edit = editMotionField(p, 1, 0, "position.az", "45");
    REQUIRE(edit);
    REQUIRE(edit->effect.kind == Kind::Source);
    Project after = p;
    edit->apply(after);
    const auto plan = planEngineSync(after, edit->effect);
    REQUIRE(commandOf<cmd::SetSourcePosition>(plan)->position.azimuth == 45.0f);
    // Smoothing is baked into the node: that one rebuilds.
    REQUIRE(editMotionField(p, 1, 0, "smoothMs", "100")->effect.kind == Kind::Structural);
}

TEST_CASE("Linked points edit their own channel's motion", "[ui][sources]") {
    Project p = makeProject();
    SourceSpec& s = p.sources[0];
    REQUIRE(setSourceField(s, p.layout, "stereo", "on"));
    REQUIRE(setSourceField(s, p.layout, "linkedPoints", "on"));
    s.motion = makeMotion(MotionKind::Orbit);
    s.motionRight = makeMotion(MotionKind::Orbit);

    auto edit = editMotionField(p, 1, 1, "radius", "6");
    REQUIRE(edit);
    REQUIRE(edit->effect.kind == Kind::MotionParam);
    REQUIRE(edit->effect.channel == 1);
    edit->apply(p);
    REQUIRE(std::get<OrbitMotion>(p.sources[0].motionRight->value).radius == 6.0f);
    REQUIRE(std::get<OrbitMotion>(p.sources[0].motion.value).radius == 1.0f);
    REQUIRE_FALSE(editMotionField(p, 1, 2, "radius", "6"));  // no such channel
    REQUIRE(valid(p));
}

TEST_CASE("One drag of a motion slider is one undo step", "[ui][sources]") {
    Project p = makeProject();
    ProjectHistory h;
    h.beginGesture();
    for (const char* v : {"2.5", "3", "3.5", "4"}) h.perform(p, *editMotionField(p, 2, 0, "radius", v));
    h.endGesture();
    REQUIRE(h.undoDepth() == 1);
    REQUIRE(std::get<OrbitMotion>(p.sources[1].motion.value).radius == 4.0f);
    h.undo(p);
    REQUIRE(std::get<OrbitMotion>(p.sources[1].motion.value).radius == 2.0f);
}

TEST_CASE("Changing the motion kind starts where the source is", "[ui][sources]") {
    Project p = makeProject();
    ManualMotion manual;
    manual.position = {30.0f, 10.0f, 3.0f};
    p.sources[0].motion.value = manual;

    auto edit = editMotionKind(p, 1, 0, MotionKind::Line);
    REQUIRE(edit);
    REQUIRE(edit->effect.kind == Kind::Structural);
    ProjectHistory h;
    h.perform(p, *edit);
    REQUIRE(std::get<LineMotion>(p.sources[0].motion.value).from == Position{30.0f, 10.0f, 3.0f});
    REQUIRE(valid(p));

    REQUIRE_FALSE(editMotionKind(p, 1, 0, MotionKind::Line));  // already a Line
    auto back = editMotionKind(p, 1, 0, MotionKind::Manual);
    REQUIRE(back);
    h.perform(p, *back);
    const auto restored =
        std::get<ManualMotion>(p.sources[0].motion.value).position;  // via Cartesian: not bit-exact
    REQUIRE_THAT(restored.azimuth, Catch::Matchers::WithinAbs(30.0, 1e-3));
    REQUIRE_THAT(restored.elevation, Catch::Matchers::WithinAbs(10.0, 1e-3));
    REQUIRE_THAT(restored.distance, Catch::Matchers::WithinAbs(3.0, 1e-3));
    h.undo(p);
    h.undo(p);
    REQUIRE(std::get<ManualMotion>(p.sources[0].motion.value).position == Position{30.0f, 10.0f, 3.0f});
}

TEST_CASE("Path points and steps are added and removed through edits", "[ui][sources]") {
    Project p = makeProject();
    ProjectHistory h;
    h.perform(p, *editMotionKind(p, 1, 0, MotionKind::Path));
    h.perform(p, *editMotionAddItem(p, 1, 0));
    REQUIRE(std::get<PathMotion>(p.sources[0].motion.value).points.size() == 3);
    h.perform(p, *editMotionRemoveItem(p, 1, 0, 0));
    REQUIRE_FALSE(editMotionRemoveItem(p, 1, 0, 0));  // minimum of two points
    REQUIRE(valid(p));
    h.undo(p);
    REQUIRE(std::get<PathMotion>(p.sources[0].motion.value).points.size() == 3);
    REQUIRE_FALSE(editMotionAddItem(makeProject(), 1, 0));  // Manual has no list
}

TEST_CASE("Modulator edits add and remove with undo", "[ui][sources]") {
    Project p = makeProject();
    ProjectHistory h;
    for (int i = 0; i < kMaxModulators; ++i) REQUIRE(h.perform(p, *editAddModulator(p, 1)));
    REQUIRE_FALSE(editAddModulator(p, 1));
    REQUIRE(h.perform(p, *editRemoveModulator(p, 1, 2)));
    REQUIRE(p.sources[0].modulators.size() == 3);
    h.undo(p);
    REQUIRE(p.sources[0].modulators.size() == 4);
    REQUIRE_FALSE(editRemoveModulator(p, 1, 9));
}

TEST_CASE("Scoped undo leaves other sources alone", "[ui][sources]") {
    Project p = makeProject();
    ProjectHistory h;
    h.perform(p, *editSourceField(p, 1, "gainDb", "-6"));
    p.sources[1].gainDb = -12.0f;  // e.g. a scene recall
    h.undo(p);
    REQUIRE(p.sources[0].gainDb == 0.0f);
    REQUIRE(p.sources[1].gainDb == -12.0f);
}
