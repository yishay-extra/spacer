#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <thread>

#include "EngineTestUtils.h"
#include "control/ControlHub.h"
#include "control/ControlPath.h"
#include "control/OscRouter.h"
#include "core/motion/ParameterPath.h"
#include "engine/BufferInput.h"
#include "engine/GraphBuilder.h"

using Catch::Approx;
using namespace spacer;
using namespace spacer::testing;

namespace {

juce::OSCMessage msg(const char* address) {
    return juce::OSCMessage(juce::OSCAddressPattern(address));
}
template <typename... Args>
juce::OSCMessage msg(const char* address, Args... args) {
    return juce::OSCMessage(juce::OSCAddressPattern(address), args...);
}

template <typename E>
const E* route(const juce::OSCMessage& m) {
    static std::optional<ControlEvent> keep;
    const auto r = OscRouter::route(m);
    REQUIRE_FALSE(r.malformed);
    keep = r.event;
    return keep ? std::get_if<E>(&*keep) : nullptr;
}

bool malformed(const juce::OSCMessage& m) {
    return OscRouter::route(m).malformed;
}

/// Project with sources "src1" (id 1, manual at 30/0/2) and "src2", speakers 1 and 2, scenes A/B/C.
Project testProject() {
    Project p = makeProject({makeSpeaker(1, -90.0f, 2.0f, 0), makeSpeaker(2, 90.0f, 2.0f, 1)});
    p.sources.push_back(makeSource(1, {30.0f, 0.0f, 2.0f}));
    p.sources.push_back(makeSource(2, {0.0f, 0.0f, 1.0f}));
    p.sources[0].distance = {1.0f, 0.5f, 0.0f};
    auto scene = [&](const char* name, float gainDb) {
        Scene s = captureScene(p, name);
        s.sources[0].gainDb = gainDb;
        return s;
    };
    p.scenes = {scene("A", 0.0f), scene("B", -12.0f), scene("C", -24.0f)};
    return p;
}

/// Dispatcher over a recording queue.
struct Rig {
    Project project = testProject();
    std::vector<Command> sent;
    bool full = false;
    ControlDispatcher dispatcher;

    Rig()
        : dispatcher(project, nullptr,
                     {[this](const Command& c) {
                          if (full) return false;
                          sent.push_back(c);
                          return true;
                      },
                      {},
                      {},
                      {},
                      {},
                      {}}) {}

    template <typename C>
    const C& only() const {
        REQUIRE(sent.size() == 1);
        const auto* c = std::get_if<C>(&sent[0]);
        REQUIRE(c != nullptr);
        return *c;
    }
};

Rig& dispatchOsc(Rig& rig, const juce::OSCMessage& m) {
    const auto r = OscRouter::route(m);
    REQUIRE_FALSE(r.malformed);
    rig.dispatcher.dispatch(*r.event);
    return rig;
}

}  // namespace

// ---- Router: every SPEC §12.1 address ---------------------------------------

TEST_CASE("OscRouter: source position, components, gain, mute, spread") {
    const auto* aed = route<ev::SetSourcePositionAed>(msg("/spacer/source/3/pos/aed", 45.0f, 10.0f, 2.5f));
    REQUIRE(aed);
    CHECK(aed->source == EntityRef{3, {}});
    CHECK(aed->position == Position{45.0f, 10.0f, 2.5f});

    const auto* xyz = route<ev::SetSourcePositionXyz>(msg("/spacer/source/3/pos/xyz", 1.0f, 2.0f, 0.5f));
    REQUIRE(xyz);
    CHECK(xyz->xyz == Cartesian{1.0f, 2.0f, 0.5f});

    const auto* az = route<ev::SetSourceParam>(msg("/spacer/source/3/az", 90.0f));
    REQUIRE(az);
    CHECK(az->path == "motion.position.az");
    CHECK(route<ev::SetSourceParam>(msg("/spacer/source/3/el", 5.0f))->path == "motion.position.el");
    CHECK(route<ev::SetSourceParam>(msg("/spacer/source/3/dist", 5.0f))->path == "motion.position.dist");

    const auto* gain = route<ev::SetSourceParam>(msg("/spacer/source/3/gain", -6.0f));
    REQUIRE(gain);
    CHECK(gain->path == "gain");
    CHECK(gain->value == -6.0f);

    CHECK(route<ev::SetSourceMute>(msg("/spacer/source/3/mute", 1))->mute);
    CHECK_FALSE(route<ev::SetSourceMute>(msg("/spacer/source/3/mute", 0))->mute);
    CHECK(route<ev::SetSourceParam>(msg("/spacer/source/3/spread", 3.0f))->value == 1.0f);  // clamped
}

TEST_CASE("OscRouter: motion parameters reuse ParameterPath names") {
    const auto* r = route<ev::SetSourceParam>(msg("/spacer/source/2/motion/radius", 1.5f));
    REQUIRE(r);
    CHECK(r->path == "motion.radius");
    CHECK(ParameterPath::parse(r->path).has_value());
}

TEST_CASE("OscRouter: source names and integer arguments") {
    const auto* g = route<ev::SetSourceParam>(msg("/spacer/source/\"lead\"/gain", 3));
    REQUIRE(g);
    CHECK(g->source.name == "lead");
    CHECK(g->value == 3.0f);
}

TEST_CASE("OscRouter: file transport") {
    CHECK(route<ev::SourceFile>(msg("/spacer/source/1/file/play"))->action == ev::SourceFile::Action::Play);
    CHECK(route<ev::SourceFile>(msg("/spacer/source/1/file/stop"))->action == ev::SourceFile::Action::Stop);
    const auto* seek = route<ev::SourceFile>(msg("/spacer/source/1/file/seek", 12.5f));
    REQUIRE(seek);
    CHECK(seek->action == ev::SourceFile::Action::Seek);
    CHECK(seek->seconds == Approx(12.5));
}

TEST_CASE("OscRouter: speakers, master, panic") {
    CHECK(route<ev::SetSpeakerTrim>(msg("/spacer/speaker/2/trim", -3.0f))->trimDb == -3.0f);
    CHECK(route<ev::SetSpeakerMute>(msg("/spacer/speaker/2/mute", 1))->mute);
    CHECK(route<ev::SetMasterGain>(msg("/spacer/master/gain", -9.0f))->gainDb == -9.0f);
    CHECK(route<ev::Panic>(msg("/spacer/panic"))->engaged);
    CHECK_FALSE(route<ev::Panic>(msg("/spacer/panic", 0))->engaged);
}

TEST_CASE("OscRouter: cues, scenes, clock") {
    CHECK(route<ev::CueGo>(msg("/spacer/cue/go")));
    CHECK(route<ev::CueBack>(msg("/spacer/cue/back")));
    CHECK(route<ev::CueJump>(msg("/spacer/cue/jump", 4))->index == 4);
    CHECK(route<ev::CueJump>(msg("/spacer/cue/jump", juce::String("Intro")))->name == "Intro");
    const auto* s = route<ev::RecallScene>(msg("/spacer/scene/recall", juce::String("B"), 2.5f));
    REQUIRE(s);
    CHECK(s->name == "B");
    CHECK(s->seconds == 2.5f);
    CHECK(route<ev::RecallScene>(msg("/spacer/scene/recall", juce::String("B")))->seconds == 0.0f);
    CHECK(route<ev::ClockBpm>(msg("/spacer/clock/bpm", 140.0f))->bpm == Approx(140.0));
    CHECK(route<ev::ClockPlay>(msg("/spacer/clock/play")));
    CHECK(route<ev::ClockStop>(msg("/spacer/clock/stop")));
    CHECK(route<ev::ClockContinue>(msg("/spacer/clock/continue")));
    CHECK(route<ev::ClockBeat>(msg("/spacer/clock/beat", 8.0f))->beat == Approx(8.0));
}

TEST_CASE("OscRouter: bad arguments are malformed, unknown addresses become triggers") {
    CHECK(malformed(msg("/spacer/source/1/gain")));                        // missing arg
    CHECK(malformed(msg("/spacer/source/1/gain", juce::String("loud"))));  // wrong type
    CHECK(malformed(msg("/spacer/source/1/gain", 1.0f, 2.0f)));            // extra arg
    CHECK(malformed(msg("/spacer/source/1/pos/aed", 1.0f, 2.0f)));
    CHECK(malformed(msg("/spacer/source/1/gain", std::numeric_limits<float>::quiet_NaN())));
    CHECK(malformed(msg("/spacer/cue/go", 1)));
    CHECK(malformed(msg("/spacer/cue/jump", 1.5f)));
    CHECK(malformed(msg("/spacer/scene/recall")));
    CHECK(malformed(msg("/spacer/scene/recall", juce::String("B"), -1.0f)));
    CHECK(malformed(msg("/spacer/clock/bpm", 0.0f)));

    const auto* unknown = route<ev::OscTrigger>(msg("/spacer/nothing/here", 1));
    REQUIRE(unknown);
    CHECK(unknown->address == "/spacer/nothing/here");
    CHECK(route<ev::OscTrigger>(msg("/other/app", 1)));
    CHECK(route<ev::OscTrigger>(msg("/spacer/source/0/gain", 1.0f)));  // ids start at 1
    CHECK(route<ev::OscTrigger>(msg("/spacer/source/1/bogus", 1.0f)));
}

// ---- Server: bundles, counters, real UDP -------------------------------------

TEST_CASE("OscServer: counts malformed messages and never crashes on junk") {
    OscServer server;
    server.handleMessage(msg("/spacer/source/1/gain", juce::String("x")));
    server.handleMessage(msg("/spacer/source/1/gain", -3.0f));
    server.handleMalformedPacket();
    std::vector<ControlEvent> got;
    server.pump(1.0, [&](const ControlEvent& e) { got.push_back(e); });
    CHECK(got.size() == 1);
    const auto s = server.stats();
    CHECK(s.messagesReceived == 2);
    CHECK(s.malformedMessages == 1);
    CHECK(s.malformedPackets == 1);
}

TEST_CASE("OscServer: bundle timetags hold events until due") {
    OscServer server;
    const auto future = juce::OSCTimeTag(juce::Time(10'000));  // epoch +10 s
    const auto past = juce::OSCTimeTag(juce::Time(2'000));
    juce::OSCBundle later(future);
    later.addElement(msg("/spacer/master/gain", -6.0f));
    juce::OSCBundle earlier(past);
    earlier.addElement(msg("/spacer/master/gain", -3.0f));
    juce::OSCBundle outer;  // immediately
    outer.addElement(later);
    outer.addElement(earlier);
    outer.addElement(msg("/spacer/master/gain", 0.0f));
    server.handleBundle(outer);

    std::vector<float> order;
    auto deliver = [&](const ControlEvent& e) { order.push_back(std::get<ev::SetMasterGain>(e).gainDb); };
    server.pump(5.0, deliver);
    CHECK(order == std::vector<float>{0.0f, -3.0f});  // immediate first, then the past-due bundle
    server.pump(9.9, deliver);
    CHECK(order.size() == 2);
    server.pump(10.0, deliver);
    CHECK(order == std::vector<float>{0.0f, -3.0f, -6.0f});
}

TEST_CASE("OscServer: receives UDP datagrams, including malformed ones") {
    OscServer server;
    int port = 0;
    for (int p = 47100; p < 47200 && port == 0; ++p)
        if (server.start(p)) port = p;
    REQUIRE(port != 0);

    juce::OSCSender sender;
    REQUIRE(sender.connect("127.0.0.1", port));
    REQUIRE(sender.send(msg("/spacer/master/gain", -12.0f)));
    juce::DatagramSocket junk(false);
    const char garbage[] = "not an osc packet at all";
    junk.write("127.0.0.1", port, garbage, static_cast<int>(sizeof garbage));

    std::vector<ControlEvent> got;
    for (int i = 0; i < 200 && (got.empty() || server.stats().malformedPackets == 0); ++i) {
        server.pump(1e12, [&](const ControlEvent& e) { got.push_back(e); });
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    REQUIRE(got.size() == 1);
    CHECK(std::get<ev::SetMasterGain>(got[0]).gainDb == -12.0f);
    CHECK(server.stats().malformedPackets == 1);
}

// ---- Dispatcher: events → engine commands --------------------------------------

TEST_CASE("ControlDispatcher: source parameters become commands") {
    Rig rig;
    dispatchOsc(rig, msg("/spacer/source/1/gain", -6.0f));
    CHECK(rig.only<cmd::SetSourceGain>().gainDb == -6.0f);
    CHECK(rig.only<cmd::SetSourceGain>().sourceId == 1);

    rig.sent.clear();
    dispatchOsc(rig, msg("/spacer/source/\"src2\"/mute", 1));
    CHECK(rig.only<cmd::SetSourceMute>().sourceId == 2);
    CHECK(rig.only<cmd::SetSourceMute>().mute);

    rig.sent.clear();
    dispatchOsc(rig, msg("/spacer/source/1/spread", 0.4f));
    CHECK(rig.only<cmd::SetSourceSpread>().spread == Approx(0.4f));
}

TEST_CASE("ControlDispatcher: positions, single components keep the others") {
    Rig rig;
    dispatchOsc(rig, msg("/spacer/source/1/pos/aed", 190.0f, 10.0f, 3.0f));
    CHECK(rig.only<cmd::SetSourcePosition>().position == Position{-170.0f, 10.0f, 3.0f});  // wrapped

    rig.sent.clear();
    dispatchOsc(rig, msg("/spacer/source/1/az", 45.0f));
    CHECK(rig.only<cmd::SetSourcePosition>().position == Position{45.0f, 10.0f, 3.0f});

    rig.sent.clear();
    dispatchOsc(rig, msg("/spacer/source/1/pos/xyz", 0.0f, 2.0f, 0.0f));
    const auto p = rig.only<cmd::SetSourcePosition>().position;
    CHECK(p.azimuth == Approx(0.0f).margin(1e-4));
    CHECK(p.distance == Approx(2.0f));

    rig.sent.clear();
    dispatchOsc(rig, msg("/spacer/source/2/el", 20.0f));  // starts from the project's manual position
    CHECK(rig.only<cmd::SetSourcePosition>().position == Position{0.0f, 20.0f, 1.0f});
}

TEST_CASE("ControlDispatcher: distance parameters edit one field of the model") {
    Rig rig;
    rig.dispatcher.dispatch(ev::SetSourceParam{{1, {}}, "distance.k", 2.0f});
    CHECK(rig.only<cmd::SetSourceDistance>().model == DistanceModel{2.0f, 0.5f, 0.0f});
    rig.sent.clear();
    rig.dispatcher.dispatch(ev::SetSourceParam{{1, {}}, "distance.rMin", 1.0f});
    CHECK(rig.only<cmd::SetSourceDistance>().model == DistanceModel{2.0f, 1.0f, 0.0f});
}

TEST_CASE("ControlDispatcher: speakers, master, panic, clock") {
    Rig rig;
    dispatchOsc(rig, msg("/spacer/speaker/2/trim", -3.0f));
    CHECK(rig.only<cmd::SetSpeakerTrim>().speakerId == 2);
    rig.sent.clear();
    dispatchOsc(rig, msg("/spacer/speaker/\"1\"/mute", 1));
    CHECK(rig.only<cmd::SetSpeakerMute>().speakerId == 1);
    rig.sent.clear();
    dispatchOsc(rig, msg("/spacer/master/gain", -6.0f));
    CHECK(rig.only<cmd::SetMasterGain>().gainDb == -6.0f);
    rig.sent.clear();
    dispatchOsc(rig, msg("/spacer/panic"));
    CHECK(rig.only<cmd::SetPanic>().engaged);
    rig.sent.clear();
    dispatchOsc(rig, msg("/spacer/clock/bpm", 99.0f));
    CHECK(rig.only<cmd::ClockSetBpm>().bpm == Approx(99.0));
    rig.sent.clear();
    dispatchOsc(rig, msg("/spacer/clock/play"));
    REQUIRE(std::holds_alternative<cmd::ClockPlay>(rig.sent[0]));
    rig.sent.clear();
    dispatchOsc(rig, msg("/spacer/clock/stop"));
    REQUIRE(std::holds_alternative<cmd::ClockStop>(rig.sent[0]));
    rig.sent.clear();
    dispatchOsc(rig, msg("/spacer/clock/continue"));
    REQUIRE(std::holds_alternative<cmd::ClockContinue>(rig.sent[0]));
    rig.sent.clear();
    dispatchOsc(rig, msg("/spacer/clock/beat", 16.0f));
    CHECK(rig.only<cmd::ClockSetBeat>().beat == Approx(16.0));
}

TEST_CASE("ControlDispatcher: unknown targets and unsupported controls are counted, not sent") {
    Rig rig;
    dispatchOsc(rig, msg("/spacer/source/99/gain", 0.0f));
    dispatchOsc(rig, msg("/spacer/speaker/99/trim", 0.0f));
    dispatchOsc(rig, msg("/spacer/source/1/motion/period", 4.0f));  // not a ParameterPath
    dispatchOsc(rig, msg("/spacer/cue/go"));                        // no cue engine
    dispatchOsc(rig, msg("/spacer/made/up"));
    CHECK(rig.sent.empty());
    const auto s = rig.dispatcher.stats();
    CHECK(s.unresolvedTarget == 2);
    CHECK(s.badPath == 1);
    CHECK(s.unsupported == 1);
    CHECK(s.unknownAddress == 1);
}

TEST_CASE("ControlDispatcher: unsupported controls reach their hooks when provided") {
    Project project = testProject();
    int motion = 0, file = 0;
    ControlDispatcher d(project, nullptr,
                        {[](const Command&) { return true; },
                         {},
                         {},
                         [&](int id, MotionParam p, float v) {
                             motion = id;
                             return p == MotionParam::Radius && v == 4.0f;
                         },
                         [&](int id, ev::SourceFile::Action a, double) {
                             file = id;
                             return a == ev::SourceFile::Action::Play;
                         },
                         {}});
    d.dispatch(ev::SetSourceParam{{2, {}}, "motion.radius", 4.0f});
    d.dispatch(ev::SourceFile{{2, {}}, ev::SourceFile::Action::Play, 0.0});
    CHECK(motion == 2);
    CHECK(file == 2);
    CHECK(d.stats().unsupported == 0);
}

TEST_CASE("ControlDispatcher: a full command queue is counted") {
    Rig rig;
    rig.full = true;
    dispatchOsc(rig, msg("/spacer/master/gain", -6.0f));
    CHECK(rig.dispatcher.stats().commandsDropped == 1);
}

// ---- Cues and scenes through the real cue engine ---------------------------------

namespace {
struct CueRig {
    Project project = testProject();
    EngineProcessor engine;
    std::unique_ptr<CueEngine> cues;
    std::unique_ptr<ControlDispatcher> dispatcher;

    CueRig() {
        CueList l;
        l.name = "Show";
        for (const char* scene : {"B", "C"}) {
            Cue c;
            c.name = std::string("cue-") + scene;
            c.sceneName = scene;
            c.transitionSeconds = 0.0f;
            l.cues.push_back(c);
        }
        Cue osc;
        osc.name = "osc-cue";
        osc.sceneName = "A";
        osc.trigger = Trigger{trigger::Osc{"/show/reset"}};
        l.cues.push_back(osc);
        Cue note;
        note.name = "note-cue";
        note.sceneName = "B";
        note.trigger = Trigger{trigger::Midi{trigger::Midi::Type::Note, 60, 1}};
        l.cues.push_back(note);
        project.cueLists.push_back(l);

        GraphBuilder b(project);
        engine.setGraphWhileStopped(b.build(48000.0, 128));
        CueEngine::Config cfg;
        cues = std::make_unique<CueEngine>(engine, project, cfg);
        cues->armFirst(CueTick{});
        dispatcher = std::make_unique<ControlDispatcher>(
            project, cues.get(),
            ControlDispatcher::Config{[this](const Command& c) { return engine.push(c); },
                                      [] { return CueTick{}; },
                                      {},
                                      {},
                                      {},
                                      {}});
    }
    float gain() const { return project.sources[0].gainDb; }
};
}  // namespace

TEST_CASE("Control: cue go / back / jump and scene recall reach the cue engine") {
    CueRig rig;
    rig.dispatcher->dispatch(ev::CueGo{});
    CHECK(rig.gain() == -12.0f);
    rig.dispatcher->dispatch(ev::CueGo{});
    CHECK(rig.gain() == -24.0f);
    rig.dispatcher->dispatch(ev::CueBack{});
    CHECK(rig.gain() == -12.0f);
    rig.dispatcher->dispatch(ev::CueJump{1, {}});
    CHECK(rig.gain() == -24.0f);
    rig.dispatcher->dispatch(ev::CueJump{0, "cue-B"});
    CHECK(rig.gain() == -12.0f);
    rig.dispatcher->dispatch(ev::RecallScene{"A", 0.0f});
    CHECK(rig.gain() == 0.0f);
    rig.dispatcher->dispatch(ev::RecallScene{"nope", 0.0f});
    CHECK(rig.dispatcher->stats().unresolvedTarget == 1);
}

TEST_CASE("Control: Osc and Midi triggers fire the matching cue, other addresses are unknown") {
    CueRig rig;
    rig.dispatcher->dispatch(ev::CueJump{1, {}});  // next is the osc cue
    CHECK(rig.gain() == -24.0f);
    rig.dispatcher->dispatch(ev::OscTrigger{"/show/reset"});
    CHECK(rig.gain() == 0.0f);
    rig.dispatcher->dispatch(ev::OscTrigger{"/spacer/nothing"});
    CHECK(rig.dispatcher->stats().unknownAddress == 1);
    rig.dispatcher->dispatch(ev::MidiTrigger{ev::MidiTrigger::Type::Note, 60, 1});
    CHECK(rig.gain() == -12.0f);
}

TEST_CASE("Control: program change recalls scene N with no transition") {
    CueRig rig;
    rig.dispatcher->dispatch(ev::RecallSceneIndex{2, 0.0f});
    CHECK(rig.gain() == -24.0f);
    rig.dispatcher->dispatch(ev::RecallSceneIndex{7, 0.0f});
    CHECK(rig.dispatcher->stats().unresolvedTarget == 1);
}

// ---- MIDI mapper ---------------------------------------------------------------

namespace {
std::vector<ControlEvent> feed(MidiMapper& m, std::initializer_list<juce::MidiMessage> messages) {
    std::vector<ControlEvent> out;
    for (const auto& x : messages) m.handle(x, out);
    // CC messages with a value > 0 also offer themselves to cue triggers; the
    // tests here look at the parameter events.
    std::erase_if(out, [](const ControlEvent& e) {
        const auto* t = std::get_if<ev::MidiTrigger>(&e);
        return t != nullptr && t->type == ev::MidiTrigger::Type::Cc;
    });
    return out;
}
template <typename E>
const E& at(const std::vector<ControlEvent>& v, std::size_t i) {
    REQUIRE(i < v.size());
    const auto* e = std::get_if<E>(&v[i]);
    REQUIRE(e != nullptr);
    return *e;
}
MidiConfig config(std::vector<MidiMapping> mappings, int pc = 0) {
    MidiConfig c;
    c.mappings = std::move(mappings);
    c.programChangeChannel = pc;
    return c;
}
using juce::MidiMessage;
}  // namespace

TEST_CASE("MidiMapper: 7-bit CC with range and channel filter") {
    MidiMapper m;
    m.setConfig(config({{MidiMessageType::Cc, 2, 7, "source.1.gain", -60.0f, 6.0f, MidiCurve::Linear}}));
    auto out = feed(m, {MidiMessage::controllerEvent(2, 7, 127), MidiMessage::controllerEvent(2, 7, 0),
                        MidiMessage::controllerEvent(3, 7, 64)});
    REQUIRE(out.size() == 2);
    CHECK(at<ev::SetSourceParam>(out, 0).value == Approx(6.0f));
    CHECK(at<ev::SetSourceParam>(out, 0).path == "gain");
    CHECK(at<ev::SetSourceParam>(out, 0).source.id == 1);
    CHECK(at<ev::SetSourceParam>(out, 1).value == Approx(-60.0f));
    CHECK(m.stats().unmapped == 1);  // channel 3
}

TEST_CASE("MidiMapper: log curve and channel 0 (any)") {
    MidiMapper m;
    m.setConfig(config({{MidiMessageType::Cc, 0, 20, "master.gain", 1.0f, 100.0f, MidiCurve::Log}}));
    const auto out =
        feed(m, {MidiMessage::controllerEvent(9, 20, 0), MidiMessage::controllerEvent(9, 20, 127)});
    // 64/127 would be the geometric middle; endpoints and midpoint are exact for a log curve.
    CHECK(at<ev::SetMasterGain>(out, 0).gainDb == Approx(1.0f));
    CHECK(at<ev::SetMasterGain>(out, 1).gainDb == Approx(100.0f));
    const auto mid = feed(m, {MidiMessage::controllerEvent(1, 20, 64)});
    CHECK(at<ev::SetMasterGain>(mid, 0).gainDb == Approx(std::pow(100.0f, 64.0f / 127.0f)).epsilon(1e-4));
}

TEST_CASE("MidiMapper: 14-bit CC pairs MSB and LSB") {
    MidiMapper m;
    m.setConfig(config({{MidiMessageType::Cc14, 1, 3, "source.2.spread", 0.0f, 1.0f, MidiCurve::Linear}}));
    const auto out =
        feed(m, {MidiMessage::controllerEvent(1, 3, 127), MidiMessage::controllerEvent(1, 35, 127)});
    REQUIRE(out.size() >= 2);
    CHECK(at<ev::SetSourceParam>(out, out.size() - 1).value == Approx(1.0f));
    const auto half =
        feed(m, {MidiMessage::controllerEvent(1, 3, 64), MidiMessage::controllerEvent(1, 35, 0)});
    CHECK(at<ev::SetSourceParam>(half, half.size() - 1).value == Approx(8192.0f / 16383.0f));
}

TEST_CASE("MidiMapper: NRPN selection and data entry") {
    MidiMapper m;
    m.setConfig(
        config({{MidiMessageType::Nrpn, 1, 300, "speaker.2.trim", -12.0f, 12.0f, MidiCurve::Linear}}));
    // 300 = (2 << 7) | 44
    const auto out =
        feed(m, {MidiMessage::controllerEvent(1, 99, 2), MidiMessage::controllerEvent(1, 98, 44),
                 MidiMessage::controllerEvent(1, 6, 127), MidiMessage::controllerEvent(1, 38, 127)});
    REQUIRE(out.size() == 2);
    CHECK(at<ev::SetSpeakerTrim>(out, 1).trimDb == Approx(12.0f));
    CHECK(at<ev::SetSpeakerTrim>(out, 0).trimDb == Approx(-12.0f + 24.0f * (127 << 7) / 16383.0f));
    // The selection CCs never count as unmapped controllers.
    CHECK(m.stats().unmapped == 0);
}

TEST_CASE("MidiMapper: pitch bend") {
    MidiMapper m;
    m.setConfig(config({{MidiMessageType::PitchBend, 4, 0, "clock.bpm", 60.0f, 180.0f, MidiCurve::Linear}}));
    const auto out = feed(m, {MidiMessage::pitchWheel(4, 0), MidiMessage::pitchWheel(4, 16383),
                              MidiMessage::pitchWheel(5, 100)});
    REQUIRE(out.size() == 2);
    CHECK(at<ev::ClockBpm>(out, 0).bpm == Approx(60.0));
    CHECK(at<ev::ClockBpm>(out, 1).bpm == Approx(180.0));
}

TEST_CASE("MidiMapper: notes trigger cues, program change recalls scenes") {
    MidiMapper m;
    m.setConfig(config({}, 5));
    auto out = feed(m, {MidiMessage::noteOn(3, 60, 100.0f / 127.0f), MidiMessage::noteOff(3, 60),
                        MidiMessage::programChange(5, 2), MidiMessage::programChange(6, 2)});
    REQUIRE(out.size() == 2);
    const auto& note = at<ev::MidiTrigger>(out, 0);
    CHECK(note.type == ev::MidiTrigger::Type::Note);
    CHECK(note.number == 60);
    CHECK(note.channel == 3);
    CHECK(at<ev::RecallSceneIndex>(out, 1).index == 2);
    CHECK(at<ev::RecallSceneIndex>(out, 1).seconds == 0.0f);

    m.setConfig(config({}, 0));  // off by default
    CHECK(feed(m, {MidiMessage::programChange(5, 2)}).empty());
}

TEST_CASE("MidiMapper: learn captures the first moved control only") {
    MidiMapper m;
    m.setConfig(config({{MidiMessageType::Cc, 1, 9, "source.1.gain", 0.0f, 1.0f, MidiCurve::Linear}}));
    std::vector<MidiMapping> learned;
    m.onLearned = [&](const MidiMapping& x) { learned.push_back(x); };

    m.startLearn("source.2.spread", {0.0f, 0.5f, MidiCurve::Linear});
    CHECK(m.learning());
    CHECK(feed(m, {MidiMessage::noteOn(1, 60, 1.0f), MidiMessage::programChange(1, 3)}).size() ==
          1);  // only the note trigger: not learnable
    CHECK(m.learning());
    feed(m, {MidiMessage::controllerEvent(7, 21, 100), MidiMessage::controllerEvent(7, 22, 5)});
    CHECK_FALSE(m.learning());
    REQUIRE(learned.size() == 1);
    CHECK(learned[0].type == MidiMessageType::Cc);
    CHECK(learned[0].channel == 7);
    CHECK(learned[0].number == 21);
    CHECK(learned[0].parameterPath == "source.2.spread");
    CHECK(learned[0].max == 0.5f);
    REQUIRE(m.config().mappings.size() == 2);

    // The learned mapping works immediately.
    const auto out = feed(m, {MidiMessage::controllerEvent(7, 21, 127)});
    CHECK(at<ev::SetSourceParam>(out, 0).value == Approx(0.5f));

    // Re-learning the same path replaces its mapping.
    m.startLearn("source.2.spread");
    feed(m, {MidiMessage::pitchWheel(2, 8000)});
    REQUIRE(m.config().mappings.size() == 2);
    CHECK(m.config().mappings.back().type == MidiMessageType::PitchBend);
}

TEST_CASE("MidiMapper: learn captures NRPN as one control") {
    MidiMapper m;
    m.startLearn("master.gain");
    feed(m, {MidiMessage::controllerEvent(1, 99, 1), MidiMessage::controllerEvent(1, 98, 5)});
    CHECK(m.learning());  // selection alone is not a moved control
    feed(m, {MidiMessage::controllerEvent(1, 6, 10)});
    REQUIRE(m.config().mappings.size() == 1);
    CHECK(m.config().mappings[0].type == MidiMessageType::Nrpn);
    CHECK(m.config().mappings[0].number == (1 << 7 | 5));
}

TEST_CASE("MidiMapper: a mapping with a bad path is counted, never crashes") {
    MidiMapper m;
    m.setConfig(config({{MidiMessageType::Cc, 0, 1, "bogus.path", 0.0f, 1.0f, MidiCurve::Linear}}));
    CHECK(feed(m, {MidiMessage::controllerEvent(1, 1, 5)}).empty());
    CHECK(m.stats().invalidPath == 1);
    std::vector<ControlEvent> out;
    m.handle(MidiMessage::midiClock(), out);  // system messages are ignored
    CHECK(out.empty());
}

TEST_CASE("MidiMapper: CC with value > 0 also triggers Midi cues") {
    MidiMapper m;
    std::vector<ControlEvent> out;
    m.handle(MidiMessage::controllerEvent(2, 64, 127), out);
    m.handle(MidiMessage::controllerEvent(2, 64, 0), out);
    REQUIRE(out.size() == 1);
    CHECK(at<ev::MidiTrigger>(out, 0) == ev::MidiTrigger{ev::MidiTrigger::Type::Cc, 64, 2});
}

TEST_CASE("controlpath: scoped and bare paths") {
    CHECK(controlpath::isValid("gain"));
    CHECK(controlpath::isValid("source.3.motion.radius"));
    CHECK(controlpath::isValid("speaker.1.mute"));
    CHECK(controlpath::isValid("master.gain"));
    CHECK_FALSE(controlpath::isValid("source.0.gain"));
    CHECK_FALSE(controlpath::isValid("source.3.bogus"));
    CHECK_FALSE(controlpath::isValid("speaker.1.polarity"));
    CHECK_FALSE(controlpath::isValid(""));
}

// ---- Sender -----------------------------------------------------------------------

TEST_CASE("OscSender: 30 Hz bundles with the SPEC out addresses") {
    OscSender sender;
    std::vector<juce::OSCBundle> sent;
    sender.setTransport([&](const juce::OSCBundle& b) {
        sent.push_back(b);
        return true;
    });
    fb::Clock clk;
    clk.state.playing = true;
    clk.state.beat = 4.5;
    sender.onFeedback(fb::SourcePosition{3, 0, {10.0f, 5.0f, 2.0f}});
    sender.onFeedback(fb::SourcePosition{3, 1, {99.0f, 0.0f, 1.0f}});  // right channel: ignored
    sender.onFeedback(fb::SpeakerMeter{2, 0.25f, 0.0f});
    sender.onFeedback(clk);
    sender.setCurrentCue(7);

    CHECK(sender.tick(100.0));
    REQUIRE(sent.size() == 1);
    std::vector<std::string> addresses;
    for (const auto& e : sent[0])
        addresses.push_back(e.getMessage().getAddressPattern().toString().toStdString());
    CHECK(addresses == std::vector<std::string>{"/spacer/source/3/pos/aed", "/spacer/meter/speaker/2",
                                                "/spacer/clock/beat", "/spacer/cue/current"});
    const auto& pos = sent[0].begin()->getMessage();
    CHECK(pos[0].getFloat32() == 10.0f);
    CHECK(pos[2].getFloat32() == 2.0f);
    CHECK((sent[0].begin() + 3)->getMessage()[0].getInt32() == 7);

    // Nothing changed: nothing sent. Rate limited to 30 Hz.
    CHECK_FALSE(sender.tick(100.1));
    sender.onFeedback(fb::SpeakerMeter{2, 0.5f, 0.0f});
    CHECK_FALSE(sender.tick(100.02));
    CHECK(sender.tick(100.04));
    CHECK(sent.size() == 2);
}

// ---- Hub: OSC → engine, queue stays single-producer ------------------------------

TEST_CASE("ControlHub: OSC and MIDI reach the engine only through pump()") {
    Project project = testProject();
    project.midi = config({{MidiMessageType::Cc, 1, 7, "master.gain", -60.0f, 0.0f, MidiCurve::Linear}});
    EngineProcessor engine;
    GraphBuilder builder(project);
    builder.setInputFactory([](const SourceSpec&) -> std::unique_ptr<SourceInput> {
        juce::AudioBuffer<float> b(1, 48000 * 5);
        for (int i = 0; i < b.getNumSamples(); ++i) b.setSample(0, i, 0.5f);
        return std::make_unique<BufferInput>(std::move(b));
    });
    engine.setGraphWhileStopped(builder.build(48000.0, 128));

    ControlHub hub(project, engine, nullptr);
    hub.midi().setConfig(project.midi);
    auto run = [&](int blocks) {
        juce::AudioBuffer<float> buf(2, 128);
        float* p[] = {buf.getWritePointer(0), buf.getWritePointer(1)};
        for (int i = 0; i < blocks; ++i) engine.process(nullptr, 0, p, 2, 128);
        return peakAbs(p[0], 128) + peakAbs(p[1], 128);
    };
    const float before = run(40);
    REQUIRE(before > 0.0f);

    // Receiver / MIDI thread side: nothing is pushed until pump().
    hub.osc().handleMessage(msg("/spacer/master/gain", -20.0f));
    CHECK(run(40) == Approx(before));
    hub.pump(1.0, 1.0);
    const float afterOsc = run(40);
    CHECK(afterOsc == Approx(before * 0.1f).margin(before * 0.01f));

    hub.submitMidi(MidiMessage::controllerEvent(1, 7, 127));  // master.gain = 0 dB
    CHECK(run(40) == Approx(afterOsc));
    hub.pump(2.0, 2.0);
    CHECK(run(40) == Approx(before).margin(before * 0.01f));
    CHECK(hub.dispatcher().stats().commandsDropped == 0);
}

// ---- plugin parameters (WP10, SPEC §13) ---------------------------------------

TEST_CASE("OscRouter: insert parameters of sources and speakers") {
    const auto* s = route<ev::SetInsertParam>(msg("/spacer/source/3/insert/1/param/12", 0.25f));
    REQUIRE(s);
    CHECK(s->owner == InsertOwner::Source);
    CHECK(s->target == EntityRef{3, {}});
    CHECK(s->slot == 1);
    CHECK(s->index == 12);
    CHECK(s->value == 0.25f);
    const auto* k = route<ev::SetInsertParam>(msg("/spacer/speaker/2/insert/0/param/0", 1));
    REQUIRE(k);
    CHECK(k->owner == InsertOwner::Speaker);
    CHECK(k->value == 1.0f);
    CHECK(malformed(msg("/spacer/source/3/insert/1/param/12")));
    // Not an insert address (slot out of range): offered to cues as a trigger.
    CHECK(route<ev::OscTrigger>(msg("/spacer/source/3/insert/9/param/0", 0.5f)));
}

TEST_CASE("controlpath: insert parameter paths") {
    const auto e = controlpath::makeEvent("source.2.insert.0.param.4", 0.5f);
    REQUIRE(e);
    const auto& p = std::get<ev::SetInsertParam>(*e);
    CHECK(p.owner == InsertOwner::Source);
    CHECK(p.target.id == 2);
    CHECK(p.index == 4);
    CHECK(controlpath::isValid("speaker.1.insert.3.param.0"));
    CHECK(std::get<ev::SetInsertParam>(*controlpath::makeEvent("insert.1.param.2", 0.0f)).target.id == 0);
    CHECK_FALSE(controlpath::isValid("source.2.insert.0.param"));
}

TEST_CASE("ControlDispatcher: insert parameters become commands, clamped to 0..1") {
    Rig rig;
    dispatchOsc(rig, msg("/spacer/source/\"src2\"/insert/0/param/3", 1.5f));
    const auto& c = rig.only<cmd::SetInsertParam>();
    CHECK(c.owner == InsertOwner::Source);
    CHECK(c.id == 2);
    CHECK(c.slot == 0);
    CHECK(c.index == 3);
    CHECK(c.value == 1.0f);

    Rig speakers;
    dispatchOsc(speakers, msg("/spacer/speaker/2/insert/1/param/0", 0.3f));
    CHECK(speakers.only<cmd::SetInsertParam>().owner == InsertOwner::Speaker);
    Rig unknown;
    dispatchOsc(unknown, msg("/spacer/speaker/7/insert/1/param/0", 0.3f));
    CHECK(unknown.sent.empty());
    CHECK(unknown.dispatcher.stats().unresolvedTarget == 1);
}

// ---- WP14: motion parameters, file transport, ControlHub config, timetags ----

TEST_CASE("ControlDispatcher: motion parameters become SetMotionParam (WP14)") {
    Rig rig;
    dispatchOsc(rig, msg("/spacer/source/1/motion/radius", 4.0f));
    const auto& c = rig.only<cmd::SetMotionParam>();
    CHECK(c.sourceId == 1);
    CHECK(c.channel == -1);
    CHECK(c.param == MotionParam::Radius);
    CHECK(c.value == 4.0f);
    CHECK(rig.dispatcher.stats().unsupported == 0);
}

TEST_CASE("ControlDispatcher: file transport becomes FilePlay / FileStop / FileSeek (WP14)") {
    Rig rig;
    dispatchOsc(rig, msg("/spacer/source/2/file/play"));
    dispatchOsc(rig, msg("/spacer/source/2/file/stop"));
    dispatchOsc(rig, msg("/spacer/source/2/file/seek", 1.5f));
    REQUIRE(rig.sent.size() == 3);
    CHECK(std::get<cmd::FilePlay>(rig.sent[0]).sourceId == 2);
    CHECK(std::get<cmd::FileStop>(rig.sent[1]).sourceId == 2);
    CHECK(std::get<cmd::FileSeek>(rig.sent[2]).sourceId == 2);
    CHECK(std::get<cmd::FileSeek>(rig.sent[2]).seconds == Approx(1.5));
    CHECK(rig.dispatcher.stats().unsupported == 0);
}

TEST_CASE("ControlDispatcher: timed events use pushAt, untimed ones push (WP14)") {
    Project project = testProject();
    std::vector<Command> now;
    std::vector<std::pair<Command, double>> timed;
    ControlDispatcher::Config cfg;
    cfg.push = [&](const Command& c) {
        now.push_back(c);
        return true;
    };
    cfg.pushAt = [&](const Command& c, double due) {
        timed.emplace_back(c, due);
        return true;
    };
    ControlDispatcher d(project, nullptr, cfg);
    d.dispatch(ev::SetMasterGain{-6.0f}, 1234.5);
    d.dispatch(ev::SetMasterGain{-3.0f});
    REQUIRE(timed.size() == 1);
    CHECK(timed[0].second == Approx(1234.5));
    CHECK(std::get<cmd::SetMasterGain>(timed[0].first).gainDb == -6.0f);
    REQUIRE(now.size() == 1);
    CHECK(std::get<cmd::SetMasterGain>(now[0]).gainDb == -3.0f);
}

TEST_CASE("ControlHub: takes the composition root's Config (selected source, push) (WP14)") {
    Project project = testProject();
    project.midi = config({{MidiMessageType::Cc, 1, 7, "gain", -60.0f, 0.0f, MidiCurve::Linear}});
    EngineProcessor engine;
    std::vector<Command> sent;
    ControlDispatcher::Config cfg;
    cfg.selectedSource = [] { return 2; };
    cfg.push = [&](const Command& c) {
        sent.push_back(c);
        return true;
    };
    ControlHub hub(project, engine, nullptr, cfg);
    hub.midi().setConfig(project.midi);
    hub.submitMidi(MidiMessage::controllerEvent(1, 7, 127));  // bare "gain" -> selected source
    hub.osc().handleMessage(msg("/spacer/source/1/motion/radius", 2.0f));
    hub.pump(1.0, 1.0);
    REQUIRE(sent.size() == 2);
    const auto* motion = std::get_if<cmd::SetMotionParam>(&sent[0]);
    REQUIRE(motion != nullptr);
    CHECK(motion->sourceId == 1);
    const auto* gain = std::get_if<cmd::SetSourceGain>(&sent[1]);
    REQUIRE(gain != nullptr);
    CHECK(gain->sourceId == 2);
    CHECK(gain->gainDb == Approx(0.0f));
}

TEST_CASE("OscServer: look-ahead pump hands engine events over early with their timetag (WP14)") {
    OscServer server;
    juce::OSCBundle b(juce::OSCTimeTag(juce::Time(10'000)));
    b.addElement(msg("/spacer/master/gain", -6.0f));
    b.addElement(msg("/spacer/cue/go"));
    server.handleBundle(b);
    std::vector<std::pair<int, double>> got;  // (variant index, due)
    auto deliver = [&](const ControlEvent& e, double due) {
        got.emplace_back(static_cast<int>(e.index()), due);
    };
    server.pump(9.0, 0.5, deliver);
    CHECK(got.empty());
    server.pump(9.6, 0.5, deliver);  // master gain within the look-ahead, cue not yet due
    REQUIRE(got.size() == 1);
    CHECK(got[0].first == static_cast<int>(ControlEvent{ev::SetMasterGain{}}.index()));
    CHECK(got[0].second == Approx(10.0));
    server.pump(10.0, 0.5, deliver);
    REQUIRE(got.size() == 2);
    CHECK(got[1].first == static_cast<int>(ControlEvent{ev::CueGo{}}.index()));
}

namespace {
/// Constant 0.5 on source 1 (manual, at speaker 1), 48 kHz / 128.
struct TimedRig {
    Project project = testProject();
    EngineProcessor engine;
    std::vector<float> out;  // speaker 1 output, all blocks
    TimedRig() {
        project.sources.resize(1);
        project.sources[0].motion.value = ManualMotion{{-90.0f, 0.0f, 2.0f}, 0.0f};
        project.sources[0].distance = {};
        GraphBuilder builder(project);
        builder.setInputFactory([](const SourceSpec&) -> std::unique_ptr<SourceInput> {
            juce::AudioBuffer<float> b(1, 48000 * 5);
            for (int i = 0; i < b.getNumSamples(); ++i) b.setSample(0, i, 0.5f);
            return std::make_unique<BufferInput>(std::move(b));
        });
        engine.setGraphWhileStopped(builder.build(48000.0, 128));
    }
    void run(int blocks, double hostStart = -1.0) {
        juce::AudioBuffer<float> buf(2, 128);
        float* p[] = {buf.getWritePointer(0), buf.getWritePointer(1)};
        for (int i = 0; i < blocks; ++i) {
            if (hostStart >= 0.0) engine.setBlockHostTime(hostStart + i * 128.0 / 48000.0);
            engine.process(nullptr, 0, p, 2, 128);
            out.insert(out.end(), p[0], p[0] + 128);
        }
    }
    /// First sample index in `out` that differs from `steady`.
    std::int64_t firstChange(float steady) const {
        for (std::size_t i = 0; i < out.size(); ++i)
            if (std::abs(out[i] - steady) > 1e-6f) return static_cast<std::int64_t>(i);
        return static_cast<std::int64_t>(out.size());
    }
    float steady() const { return out.back(); }
};
}  // namespace

TEST_CASE("EngineProcessor: pushAt applies a command at its exact sample (WP14)") {
    TimedRig rig;
    rig.run(40);  // fade-in done, steady
    const std::int64_t start = rig.engine.snapshot().blockStartSample + 128;
    REQUIRE(start == 40 * 128);
    // Master gain / panic is applied after the speaker chains, so there is
    // no limiter look-ahead between the command and the output.
    for (const std::int64_t offset : {37, 128 + 5, 3 * 128}) {
        const std::int64_t base = rig.engine.snapshot().blockStartSample + 128;
        rig.engine.pushAt(cmd::SetPanic{true}, base + offset);
        const float steady = rig.steady();
        REQUIRE(steady > 0.1f);
        rig.out.clear();
        rig.run(8);
        CHECK(std::abs(rig.firstChange(steady) - offset) <= 1);
        rig.engine.push(cmd::SetPanic{false});
        rig.run(40);
    }
}

TEST_CASE("EngineProcessor: a pushAt sample in the past applies at the next block (WP14)") {
    TimedRig rig;
    rig.run(40);
    rig.engine.pushAt(cmd::SetPanic{true}, 5);
    const float steady = rig.steady();
    rig.out.clear();
    rig.run(4);
    CHECK(rig.firstChange(steady) <= 1);
}

TEST_CASE("EngineProcessor: host time maps to engine samples through the last block (WP14)") {
    TimedRig rig;
    CHECK(rig.engine.sampleAtHostTime(1.0) == -1);  // no host time yet
    rig.run(3, 100.0);
    const auto snap = rig.engine.snapshot();
    CHECK(snap.blockStartSample == 256);
    CHECK(snap.hostTimeSeconds == Approx(100.0 + 256.0 / 48000.0));
    CHECK(rig.engine.sampleAtHostTime(snap.hostTimeSeconds + 1000.0 / 48000.0) == 256 + 1000);
}

TEST_CASE("ControlHub: an OSC bundle timetag is applied at its sample (WP14)") {
    TimedRig rig;
    ControlHub hub(rig.project, rig.engine, nullptr);
    rig.run(40, 100.0);
    const auto snap = rig.engine.snapshot();
    // Now: epoch 5000 s == host time of the last block. Timetag 1000 samples later.
    const double epochNow = 5000.0;
    const double due = epochNow + 1000.0 / 48000.0;
    constexpr double kNtpToUnix = 2208988800.0;
    const double ntp = due + kNtpToUnix;
    const auto secs = static_cast<std::uint64_t>(ntp);
    const auto frac = static_cast<std::uint64_t>((ntp - static_cast<double>(secs)) * 4294967296.0);
    juce::OSCBundle b(juce::OSCTimeTag((secs << 32) | frac));
    b.addElement(msg("/spacer/panic"));
    hub.osc().handleBundle(b);
    hub.pump(epochNow, snap.hostTimeSeconds);
    const float steady = rig.steady();
    rig.out.clear();
    const std::int64_t base = snap.blockStartSample + 128;
    rig.run(16, snap.hostTimeSeconds + 128.0 / 48000.0);
    const std::int64_t expected = snap.blockStartSample + 1000 - base;
    CHECK(std::abs(rig.firstChange(steady) - expected) <= 1);
}
