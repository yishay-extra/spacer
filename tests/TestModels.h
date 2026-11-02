#pragma once

// Fully-populated model instances for round-trip tests.

#include "core/Json.h"
#include "core/LayoutPresets.h"
#include "core/Project.h"

namespace spacer::testing {

inline PluginSlot makeSlot() {
    PluginSlot s;
    s.pluginId = "AudioUnit:Effects/aufx,dely,appl";
    s.stateBase64 = "AAECAw==";
    s.params = {{0, 0.25f}, {7, 0.8f}};
    return s;
}

inline std::vector<MotionSpec> allMotions() {
    std::vector<MotionSpec> m;
    m.push_back({ManualMotion{{10.0f, 5.0f, 2.5f}, 35.0f}});
    m.push_back({OrbitMotion{{0, 20, 0}, 3.0f, Period::beats(16), -1, 45.0f, 10.0f, Period::seconds(7.5)}});
    m.push_back(
        {LineMotion{{-90, 0, 2}, {90, 30, 3}, Period::seconds(4), PlayMode::PingPong, Easing::Smooth}});
    m.push_back(
        {LissajousMotion{{0, 0, 1}, 1.5f, 2.0f, 0.5f, 3.0f, 2.0f, 1.0f, Period::beats(32), {0, 90, 45}}});
    m.push_back({PathMotion{{{0, 2, 0}, {2, 2, 1}, {2, 0, 2}},
                            Period::beats(8),
                            PlayMode::Once,
                            PathInterpolation::CatmullRom,
                            true}});
    m.push_back(
        {RandomWalkMotion{BoxBounds{{-1, -2, 0}, {1, 2, 3}}, 0.3f, Period::seconds(0.5), 42u, false}});
    m.push_back({RandomWalkMotion{SphereBounds{{0, 1, 1}, 2.5f}, 0.7f, Period::beats(2), 7u, true}});
    StepSequenceMotion seq;
    seq.steps = {Step{Position{30, 0, 2}, Period::beats(1), StepTransition::Jump, Period::beats(0.25), 1.0f},
                 Step{3, Period::seconds(2), StepTransition::Glide, Period::seconds(0.5), 0.5f}};
    seq.order = StepOrder::Random;
    seq.loop = false;
    seq.seed = 99u;
    m.push_back({seq});
    return m;
}

inline std::vector<ModulatorSpec> allModulators() {
    return {ModulatorSpec{LfoSource{LfoShape::Triangle, Period::beats(4)}, "motion.radius", 0.5f, 0.1f},
            ModulatorSpec{OscSource{"/mod/a"}, "gain", 6.0f, 0.0f},
            ModulatorSpec{MidiCcSource{74}, "spread", 1.0f, -0.25f}};
}

inline std::vector<SourceSpec> allSources() {
    std::vector<SourceSpec> out;

    SourceSpec a;
    a.id = 1;
    a.name = "Synth";
    a.input.value = VirtualInputSpec{{0, 1}};
    a.stereo = true;
    a.width = 80.0f;
    a.linkedPoints = true;
    a.gainDb = -6.0f;
    a.spatializer = SpatializerKind::Dbap;
    a.spread = 0.35f;
    a.distance = {1.5f, 0.75f, 0.5f};
    a.motion = allMotions()[1];
    a.motionRight = allMotions()[2];
    a.modulators = allModulators();
    a.inserts = {makeSlot(), makeSlot()};
    out.push_back(a);

    SourceSpec b;
    b.id = 2;
    b.name = "Mic";
    b.input.value = HardwareInputSpec{{4, -1}};
    b.mute = true;
    b.spatializer = SpatializerKind::Direct;
    b.directSpeakers = {1, 2};
    out.push_back(b);

    SourceSpec c;
    c.id = 3;
    c.name = "Loop";
    c.input.value = FileInputSpec{"audio/loop.wav", true, true, -3.0f, 1.5f, true};
    c.spatializer = SpatializerKind::Ambisonic;
    out.push_back(c);

    for (const auto& [gen, id] : {std::pair{GeneratorSpec{GeneratorKind::Sine, 220.0f, 1.0f}, 4},
                                  {GeneratorSpec{GeneratorKind::Pink}, 5},
                                  {GeneratorSpec{GeneratorKind::White}, 6},
                                  {GeneratorSpec{GeneratorKind::Impulse, 440.0f, 2.5f}, 7}}) {
        SourceSpec g;
        g.id = id;
        g.name = "Gen" + std::to_string(id);
        g.input.value = gen;
        out.push_back(g);
    }
    // One source per remaining motion kind.
    int id = 8;
    for (const auto& m : allMotions()) {
        SourceSpec s;
        s.id = id++;
        s.name = "M" + std::to_string(s.id);
        s.input.value = VirtualInputSpec{{s.id % 64, -1}};
        s.motion = m;
        out.push_back(s);
    }
    return out;
}

inline SpeakerChainSpec makeChain(int speakerId) {
    SpeakerChainSpec c;
    c.speakerId = speakerId;
    c.trimDb = -1.5f;
    c.userDelayMs = 2.25f;
    c.polarityInverted = true;
    c.mute = true;
    c.eq = {120.0f, 2.0f, 900.0f, -3.0f, 1.2f, 7500.0f, 4.0f};
    c.inserts = {makeSlot()};
    return c;
}

inline std::vector<Trigger> allTriggers() {
    return {Trigger{trigger::Manual{}},
            Trigger{trigger::AtBeat{3, 2.5}},
            Trigger{trigger::AfterPrevious{Period::beats(4), 2.0}},
            Trigger{trigger::AfterPrevious{Period::seconds(10), 0.0}},
            Trigger{trigger::Osc{"/cue/trigger"}},
            Trigger{trigger::Midi{trigger::Midi::Type::Note, 60, 2}},
            Trigger{trigger::Midi{trigger::Midi::Type::Cc, 20, 0}},
            Trigger{trigger::TimeOfDay{8, 5, {1, 2, 3, 4, 5}}},
            Trigger{trigger::TimeOfDay{23, 59, {}}}};
}

inline Scene makeScene(const std::string& name) {
    Scene s;
    s.name = name;
    s.sources = allSources();
    s.speakerChains = {makeChain(1), makeChain(2)};
    s.master.gainDb = -2.0f;
    return s;
}

/// Uses every field of every type. Valid under validate().
inline Project makeFullProject() {
    Project p;
    p.name = "Full";
    p.layout = *layoutpresets::load("7.1.4");
    p.devices = {"Spacer Aggregate", 96000.0, 256, 340.0f};
    p.ambisonics.order = 5;
    p.sources = allSources();
    p.speakerChains = {makeChain(1), makeChain(3)};
    p.master.gainDb = -1.0f;
    p.scenes = {makeScene("Intro"), makeScene("Outro")};
    CueList list;
    list.name = "Show";
    list.loop = true;
    int n = 0;
    for (const auto& t : allTriggers()) {
        Cue c;
        c.name = "Cue " + std::to_string(++n);
        c.sceneName = n % 2 ? "Intro" : "Outro";
        c.transitionSeconds = 2.5f;
        c.trigger = t;
        c.restartMotionPhase = n % 2 == 0;
        c.postWaitSeconds = 1.0f;
        list.cues.push_back(c);
    }
    p.cueLists = {list, CueList{"Empty", {}, false}};
    p.osc = {9100, {"10.0.0.2", 9101, true}};
    p.midi.mappings = {
        MidiMapping{MidiMessageType::Cc, 1, 7, "gain", -60.0f, 6.0f, MidiCurve::Linear},
        MidiMapping{MidiMessageType::Nrpn, 0, 300, "source.1.insert.0.param.3", 20.0f, 20000.0f,
                    MidiCurve::Log},
        MidiMapping{MidiMessageType::PitchBend, 3, 0, "spread", 0.0f, 1.0f, MidiCurve::Linear},
        MidiMapping{MidiMessageType::Cc14, 0, 12, "distance.k", 0.0f, 2.0f, MidiCurve::Linear}};
    p.clock = {ClockKind::Midi, 133.5, 3, true};
    return p;
}

template <class T>
T roundTrip(const T& v) {
    const Json j = v;
    // Through text as well, to cover number formatting.
    return Json::parse(j.dump()).get<T>();
}

}  // namespace spacer::testing
