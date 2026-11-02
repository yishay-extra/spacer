#include "core/Json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <type_traits>

namespace spacer {

namespace {

// ---- helpers ---------------------------------------------------------------

[[noreturn]] void fail(const std::string& msg) {
    throw std::runtime_error(msg);
}

/// Shortest decimal that round-trips the float, as a double, so files read
/// "0.2" and not "0.20000000298023224".
Json jf(float v) {
    if (!std::isfinite(v)) return Json(static_cast<double>(v));
    char buf[40];
    for (int prec = 6; prec <= 9; ++prec) {
        std::snprintf(buf, sizeof buf, "%.*g", prec, static_cast<double>(v));
        if (std::strtof(buf, nullptr) == v) break;
    }
    return Json(std::strtod(buf, nullptr));
}

void requireObject(const Json& j, const char* what) {
    if (!j.is_object()) fail(std::string(what) + " must be an object");
}

const Json& required(const Json& j, const char* key) {
    auto it = j.find(key);
    if (it == j.end()) fail(std::string("missing required key '") + key + "'");
    return *it;
}

template <class T>
T convert(const Json& j) {
    if constexpr (std::is_same_v<T, float>) {
        if (!j.is_number()) fail("expected a number");
        return j.get<float>();
    } else if constexpr (std::is_same_v<T, double>) {
        if (!j.is_number()) fail("expected a number");
        return j.get<double>();
    } else if constexpr (std::is_same_v<T, bool>) {
        if (!j.is_boolean()) fail("expected true/false");
        return j.get<bool>();
    } else if constexpr (std::is_integral_v<T>) {
        if (!j.is_number_integer()) fail("expected an integer");
        return j.get<T>();
    } else if constexpr (std::is_same_v<T, std::string>) {
        if (!j.is_string()) fail("expected a string");
        return j.get<std::string>();
    } else {
        return j.get<T>();
    }
}

/// Assign `out` from j[key] if present; prefix errors with the key.
template <class T>
void field(const Json& j, const char* key, T& out) {
    auto it = j.find(key);
    if (it == j.end()) return;
    try {
        out = convert<T>(*it);
    } catch (const std::exception& e) {
        fail(std::string(key) + ": " + e.what());
    }
}

template <class T>
void fieldVec(const Json& j, const char* key, std::vector<T>& out) {
    auto it = j.find(key);
    if (it == j.end()) return;
    if (!it->is_array()) fail(std::string(key) + ": expected an array");
    out.clear();
    out.reserve(it->size());
    std::size_t i = 0;
    for (const auto& e : *it) {
        try {
            out.push_back(convert<T>(e));
        } catch (const std::exception& ex) {
            fail(std::string(key) + "[" + std::to_string(i) + "]: " + ex.what());
        }
        ++i;
    }
}

template <class E, std::size_t N>
using EnumTable = std::pair<E, const char*>[N];

template <class E, std::size_t N>
const char* enumName(const EnumTable<E, N>& table, E e) {
    for (const auto& [value, name] : table)
        if (value == e) return name;
    fail("internal: unnamed enum value");
}

template <class E, std::size_t N>
E enumValue(const EnumTable<E, N>& table, const Json& j, const char* what) {
    if (!j.is_string()) fail(std::string(what) + ": expected a string");
    const auto s = j.get<std::string>();
    for (const auto& [value, name] : table)
        if (s == name) return value;
    fail(std::string(what) + ": unknown value '" + s + "'");
}

template <class E, std::size_t N>
void fieldEnum(const Json& j, const char* key, E& out, const EnumTable<E, N>& table) {
    auto it = j.find(key);
    if (it == j.end()) return;
    out = enumValue(table, *it, key);
}

const EnumTable<SourceKind, 4> kSourceKinds = {{SourceKind::VirtualInput, "VirtualInput"},
                                               {SourceKind::HardwareInput, "HardwareInput"},
                                               {SourceKind::File, "File"},
                                               {SourceKind::Generator, "Generator"}};
const EnumTable<SpatializerKind, 4> kSpatializers = {{SpatializerKind::Vbap, "Vbap"},
                                                     {SpatializerKind::Dbap, "Dbap"},
                                                     {SpatializerKind::Ambisonic, "Ambisonic"},
                                                     {SpatializerKind::Direct, "Direct"}};
const EnumTable<GeneratorKind, 4> kGenerators = {{GeneratorKind::Sine, "sine"},
                                                 {GeneratorKind::Pink, "pink"},
                                                 {GeneratorKind::White, "white"},
                                                 {GeneratorKind::Impulse, "impulse"}};
const EnumTable<PlayMode, 3> kPlayModes = {
    {PlayMode::Once, "once"}, {PlayMode::Loop, "loop"}, {PlayMode::PingPong, "pingpong"}};
const EnumTable<Easing, 2> kEasings = {{Easing::Linear, "linear"}, {Easing::Smooth, "smooth"}};
const EnumTable<PathInterpolation, 2> kInterpolations = {{PathInterpolation::Linear, "linear"},
                                                         {PathInterpolation::CatmullRom, "catmullrom"}};
const EnumTable<StepOrder, 3> kStepOrders = {
    {StepOrder::Forward, "forward"}, {StepOrder::PingPong, "pingpong"}, {StepOrder::Random, "random"}};
const EnumTable<StepTransition, 2> kTransitions = {{StepTransition::Jump, "jump"},
                                                   {StepTransition::Glide, "glide"}};
const EnumTable<LfoShape, 5> kLfoShapes = {{LfoShape::Sine, "sine"},
                                           {LfoShape::Triangle, "triangle"},
                                           {LfoShape::Saw, "saw"},
                                           {LfoShape::Square, "square"},
                                           {LfoShape::SampleAndHold, "sampleAndHold"}};
const EnumTable<MidiMessageType, 4> kMidiTypes = {{MidiMessageType::Cc, "cc"},
                                                  {MidiMessageType::Cc14, "cc14"},
                                                  {MidiMessageType::Nrpn, "nrpn"},
                                                  {MidiMessageType::PitchBend, "pitchbend"}};
const EnumTable<MidiCurve, 2> kMidiCurves = {{MidiCurve::Linear, "linear"}, {MidiCurve::Log, "log"}};
const EnumTable<ClockKind, 5> kClockKinds = {{ClockKind::Internal, "Internal"},
                                             {ClockKind::Link, "Link"},
                                             {ClockKind::Midi, "Midi"},
                                             {ClockKind::Mtc, "Mtc"},
                                             {ClockKind::Osc, "Osc"}};
const EnumTable<trigger::Midi::Type, 2> kTriggerMidiTypes = {{trigger::Midi::Type::Note, "note"},
                                                             {trigger::Midi::Type::Cc, "cc"}};

template <class T>
T parse(const Json& j) {
    return j.get<T>();
}

}  // namespace

// ---- geometry --------------------------------------------------------------

void to_json(Json& j, const Cartesian& v) {
    j = Json{{"x", jf(v.x)}, {"y", jf(v.y)}, {"z", jf(v.z)}};
}
void from_json(const Json& j, Cartesian& v) {
    requireObject(j, "position");
    v = {};
    field(j, "x", v.x);
    field(j, "y", v.y);
    field(j, "z", v.z);
}

void to_json(Json& j, const Position& v) {
    j = Json{{"az", jf(v.azimuth)}, {"el", jf(v.elevation)}, {"dist", jf(v.distance)}};
}
void from_json(const Json& j, Position& v) {
    requireObject(j, "position");
    v = {};
    field(j, "az", v.azimuth);
    field(j, "el", v.elevation);
    field(j, "dist", v.distance);
}

void to_json(Json& j, const Speaker& v) {
    j = Json{{"id", v.id},
             {"name", v.name},
             {"position", v.position},
             {"trimDb", jf(v.trimDb)},
             {"userDelayMs", jf(v.userDelayMs)},
             {"polarityInverted", v.polarityInverted},
             {"outputChannel", v.outputChannel}};
}
void from_json(const Json& j, Speaker& v) {
    requireObject(j, "speaker");
    v = {};
    v.id = convert<int>(required(j, "id"));
    field(j, "name", v.name);
    field(j, "position", v.position);
    field(j, "trimDb", v.trimDb);
    field(j, "userDelayMs", v.userDelayMs);
    field(j, "polarityInverted", v.polarityInverted);
    field(j, "outputChannel", v.outputChannel);
}

void to_json(Json& j, const Layout& v) {
    j = Json{{"name", v.name}, {"speakers", v.speakers}};
}
void from_json(const Json& j, Layout& v) {
    requireObject(j, "layout");
    v = {};
    field(j, "name", v.name);
    if (auto it = j.find("speakers"); it != j.end()) {
        if (!it->is_array()) fail("speakers: expected an array");
        std::size_t i = 0;
        for (const auto& e : *it) {
            try {
                v.speakers.push_back(e.get<Speaker>());
            } catch (const std::exception& ex) {
                fail("speakers[" + std::to_string(i) + "]: " + ex.what());
            }
            ++i;
        }
    }
}

// ---- period / motion -------------------------------------------------------

void to_json(Json& j, const Period& v) {
    j = Json::object();
    j[v.unit == Period::Unit::Beats ? "beats" : "seconds"] = v.value;
}
void from_json(const Json& j, Period& v) {
    requireObject(j, "period");
    if (j.contains("beats")) {
        v = Period::beats(convert<double>(j.at("beats")));
    } else if (j.contains("seconds")) {
        v = Period::seconds(convert<double>(j.at("seconds")));
    } else {
        fail("period needs \"beats\" or \"seconds\"");
    }
}

void to_json(Json& j, const ManualMotion& v) {
    j = Json{{"position", v.position}, {"smoothMs", jf(v.smoothMs)}};
}
void from_json(const Json& j, ManualMotion& v) {
    requireObject(j, "motion");
    v = {};
    field(j, "position", v.position);
    field(j, "smoothMs", v.smoothMs);
}

void to_json(Json& j, const OrbitMotion& v) {
    j = Json{{"centre", v.centre},
             {"radius", jf(v.radius)},
             {"period", v.period},
             {"direction", v.direction},
             {"phaseOffset", jf(v.phaseOffset)},
             {"elevationWobble", jf(v.elevationWobble)},
             {"wobblePeriod", v.wobblePeriod}};
}
void from_json(const Json& j, OrbitMotion& v) {
    requireObject(j, "motion");
    v = {};
    field(j, "centre", v.centre);
    field(j, "radius", v.radius);
    field(j, "period", v.period);
    field(j, "direction", v.direction);
    field(j, "phaseOffset", v.phaseOffset);
    field(j, "elevationWobble", v.elevationWobble);
    field(j, "wobblePeriod", v.wobblePeriod);
}

void to_json(Json& j, const LineMotion& v) {
    j = Json{{"from", v.from},
             {"to", v.to},
             {"period", v.period},
             {"mode", enumName(kPlayModes, v.mode)},
             {"easing", enumName(kEasings, v.easing)}};
}
void from_json(const Json& j, LineMotion& v) {
    requireObject(j, "motion");
    v = {};
    field(j, "from", v.from);
    field(j, "to", v.to);
    field(j, "period", v.period);
    fieldEnum(j, "mode", v.mode, kPlayModes);
    fieldEnum(j, "easing", v.easing, kEasings);
}

void to_json(Json& j, const LissajousMotion& v) {
    j = Json{{"centre", v.centre},
             {"ax", jf(v.ax)},
             {"ay", jf(v.ay)},
             {"az", jf(v.az)},
             {"fx", jf(v.fx)},
             {"fy", jf(v.fy)},
             {"fz", jf(v.fz)},
             {"period", v.period},
             {"phases", Json::array({jf(v.phases[0]), jf(v.phases[1]), jf(v.phases[2])})}};
}
void from_json(const Json& j, LissajousMotion& v) {
    requireObject(j, "motion");
    v = {};
    field(j, "centre", v.centre);
    field(j, "ax", v.ax);
    field(j, "ay", v.ay);
    field(j, "az", v.az);
    field(j, "fx", v.fx);
    field(j, "fy", v.fy);
    field(j, "fz", v.fz);
    field(j, "period", v.period);
    std::vector<float> ph;
    fieldVec(j, "phases", ph);
    if (!ph.empty()) {
        if (ph.size() != 3) fail("phases: expected 3 values");
        v.phases = {ph[0], ph[1], ph[2]};
    }
}

void to_json(Json& j, const PathMotion& v) {
    j = Json{{"points", v.points},
             {"period", v.period},
             {"mode", enumName(kPlayModes, v.mode)},
             {"interpolation", enumName(kInterpolations, v.interpolation)},
             {"constantSpeed", v.constantSpeed}};
}
void from_json(const Json& j, PathMotion& v) {
    requireObject(j, "motion");
    v = {};
    if (auto it = j.find("points"); it != j.end()) {
        if (!it->is_array()) fail("points: expected an array");
        std::size_t i = 0;
        for (const auto& e : *it) {
            try {
                v.points.push_back(e.get<Cartesian>());
            } catch (const std::exception& ex) {
                fail("points[" + std::to_string(i) + "]: " + ex.what());
            }
            ++i;
        }
    }
    field(j, "period", v.period);
    fieldEnum(j, "mode", v.mode, kPlayModes);
    fieldEnum(j, "interpolation", v.interpolation, kInterpolations);
    field(j, "constantSpeed", v.constantSpeed);
}

void to_json(Json& j, const RandomWalkMotion& v) {
    Json b;
    if (const auto* box = std::get_if<BoxBounds>(&v.bounds)) {
        b = Json{{"shape", "box"}, {"min", box->min}, {"max", box->max}};
    } else {
        const auto& s = std::get<SphereBounds>(v.bounds);
        b = Json{{"shape", "sphere"}, {"centre", s.centre}, {"radius", jf(s.radius)}};
    }
    j = Json{{"bounds", b},
             {"stepMetres", jf(v.stepMetres)},
             {"stepPeriod", v.stepPeriod},
             {"seed", v.seed},
             {"glide", v.glide}};
}
void from_json(const Json& j, RandomWalkMotion& v) {
    requireObject(j, "motion");
    v = {};
    if (auto it = j.find("bounds"); it != j.end()) {
        try {
            requireObject(*it, "bounds");
            const auto shape = convert<std::string>(required(*it, "shape"));
            if (shape == "box") {
                BoxBounds b;
                field(*it, "min", b.min);
                field(*it, "max", b.max);
                v.bounds = b;
            } else if (shape == "sphere") {
                SphereBounds s;
                field(*it, "centre", s.centre);
                field(*it, "radius", s.radius);
                v.bounds = s;
            } else {
                fail("unknown shape '" + shape + "'");
            }
        } catch (const std::exception& e) {
            fail(std::string("bounds: ") + e.what());
        }
    }
    field(j, "stepMetres", v.stepMetres);
    field(j, "stepPeriod", v.stepPeriod);
    field(j, "seed", v.seed);
    field(j, "glide", v.glide);
}

void to_json(Json& j, const Step& v) {
    Json target;
    if (const auto* speaker = std::get_if<int>(&v.target)) {
        target = Json{{"speakerId", *speaker}};
    } else {
        target = std::get<Position>(v.target);
    }
    j = Json{{"target", target},
             {"duration", v.duration},
             {"transition", enumName(kTransitions, v.transition)},
             {"glideTime", v.glideTime},
             {"probability", jf(v.probability)}};
}
void from_json(const Json& j, Step& v) {
    requireObject(j, "step");
    v = {};
    if (auto it = j.find("target"); it != j.end()) {
        try {
            requireObject(*it, "target");
            if (it->contains("speakerId")) {
                v.target = convert<int>(it->at("speakerId"));
            } else {
                v.target = it->get<Position>();
            }
        } catch (const std::exception& e) {
            fail(std::string("target: ") + e.what());
        }
    }
    field(j, "duration", v.duration);
    fieldEnum(j, "transition", v.transition, kTransitions);
    field(j, "glideTime", v.glideTime);
    field(j, "probability", v.probability);
}

void to_json(Json& j, const StepSequenceMotion& v) {
    j = Json{
        {"steps", v.steps}, {"order", enumName(kStepOrders, v.order)}, {"loop", v.loop}, {"seed", v.seed}};
}
void from_json(const Json& j, StepSequenceMotion& v) {
    requireObject(j, "motion");
    v = {};
    if (auto it = j.find("steps"); it != j.end()) {
        if (!it->is_array()) fail("steps: expected an array");
        std::size_t i = 0;
        for (const auto& e : *it) {
            try {
                v.steps.push_back(e.get<Step>());
            } catch (const std::exception& ex) {
                fail("steps[" + std::to_string(i) + "]: " + ex.what());
            }
            ++i;
        }
    }
    fieldEnum(j, "order", v.order, kStepOrders);
    field(j, "loop", v.loop);
    field(j, "seed", v.seed);
}

namespace {
const char* motionKindName(const MotionSpec& m) {
    static constexpr const char* names[] = {"Manual", "Orbit",      "Line",        "Lissajous",
                                            "Path",   "RandomWalk", "StepSequence"};
    return names[m.value.index()];
}
}  // namespace

void to_json(Json& j, const MotionSpec& v) {
    std::visit([&j](const auto& m) { j = m; }, v.value);
    j["kind"] = motionKindName(v);
}
void from_json(const Json& j, MotionSpec& v) {
    requireObject(j, "motion");
    const auto kind = convert<std::string>(required(j, "kind"));
    if (kind == "Manual")
        v.value = parse<ManualMotion>(j);
    else if (kind == "Orbit")
        v.value = parse<OrbitMotion>(j);
    else if (kind == "Line")
        v.value = parse<LineMotion>(j);
    else if (kind == "Lissajous")
        v.value = parse<LissajousMotion>(j);
    else if (kind == "Path")
        v.value = parse<PathMotion>(j);
    else if (kind == "RandomWalk")
        v.value = parse<RandomWalkMotion>(j);
    else if (kind == "StepSequence")
        v.value = parse<StepSequenceMotion>(j);
    else
        fail("kind: unknown motion kind '" + kind + "'");
}

void to_json(Json& j, const ModulatorSpec& v) {
    Json s;
    if (const auto* lfo = std::get_if<LfoSource>(&v.source)) {
        s = Json{{"kind", "lfo"}, {"shape", enumName(kLfoShapes, lfo->shape)}, {"period", lfo->period}};
    } else if (const auto* osc = std::get_if<OscSource>(&v.source)) {
        s = Json{{"kind", "osc"}, {"address", osc->address}};
    } else {
        s = Json{{"kind", "midi"}, {"cc", std::get<MidiCcSource>(v.source).cc}};
    }
    j = Json{{"source", s}, {"target", v.target}, {"depth", jf(v.depth)}, {"offset", jf(v.offset)}};
}
void from_json(const Json& j, ModulatorSpec& v) {
    requireObject(j, "modulator");
    v = {};
    try {
        const Json& s = required(j, "source");
        requireObject(s, "source");
        const auto kind = convert<std::string>(required(s, "kind"));
        if (kind == "lfo") {
            LfoSource lfo;
            fieldEnum(s, "shape", lfo.shape, kLfoShapes);
            field(s, "period", lfo.period);
            v.source = lfo;
        } else if (kind == "osc") {
            OscSource osc;
            field(s, "address", osc.address);
            v.source = osc;
        } else if (kind == "midi") {
            MidiCcSource m;
            field(s, "cc", m.cc);
            v.source = m;
        } else {
            fail("unknown kind '" + kind + "'");
        }
    } catch (const std::exception& e) {
        fail(std::string("source: ") + e.what());
    }
    field(j, "target", v.target);
    field(j, "depth", v.depth);
    field(j, "offset", v.offset);
}

// ---- plugin / source -------------------------------------------------------

void to_json(Json& j, const PluginSlot& v) {
    Json params = Json::object();
    for (const auto& [index, value] : v.params) params[std::to_string(index)] = jf(value);
    j = Json{{"pluginId", v.pluginId}, {"stateBase64", v.stateBase64}, {"params", params}};
}
void from_json(const Json& j, PluginSlot& v) {
    requireObject(j, "insert");
    v = {};
    field(j, "pluginId", v.pluginId);
    field(j, "stateBase64", v.stateBase64);
    if (auto it = j.find("params"); it != j.end()) {
        requireObject(*it, "params");
        for (const auto& [key, value] : it->items()) {
            try {
                std::size_t used = 0;
                const int index = std::stoi(key, &used);
                if (used != key.size()) fail("bad index");
                v.params[index] = convert<float>(value);
            } catch (const std::exception&) {
                fail("params: invalid entry '" + key + "'");
            }
        }
    }
}

void to_json(Json& j, const DistanceModel& v) {
    j = Json{{"k", jf(v.k)}, {"rMin", jf(v.rMin)}, {"q", jf(v.q)}};
}
void from_json(const Json& j, DistanceModel& v) {
    requireObject(j, "distance");
    v = {};
    field(j, "k", v.k);
    field(j, "rMin", v.rMin);
    field(j, "q", v.q);
}

Json inputToJson(const InputSpec& input) {
    Json j = Json::object();
    auto channels = [&j](const ChannelInput& c) {
        if (c.isStereo()) {
            j["left"] = c.left;
            j["right"] = c.right;
        } else {
            j["channel"] = c.left;
        }
    };
    if (const auto* v = std::get_if<VirtualInputSpec>(&input.value)) {
        channels(*v);
    } else if (const auto* h = std::get_if<HardwareInputSpec>(&input.value)) {
        channels(*h);
    } else if (const auto* f = std::get_if<FileInputSpec>(&input.value)) {
        j["path"] = f->path;
        if (f->relativeToProject) j["relativeTo"] = "project";
        j["loop"] = f->loop;
        j["gainDb"] = jf(f->gainDb);
        j["startSeconds"] = jf(f->startSeconds);
        j["playOnSceneRecall"] = f->playOnSceneRecall;
    } else {
        const auto& g = std::get<GeneratorSpec>(input.value);
        j["type"] = enumName(kGenerators, g.type);
        if (g.type == GeneratorKind::Sine) j["hz"] = jf(g.hz);
        if (g.type == GeneratorKind::Impulse) j["periodSeconds"] = jf(g.periodSeconds);
    }
    return j;
}

InputSpec inputFromJson(SourceKind kind, const Json& j) {
    requireObject(j, "input");
    InputSpec out;
    auto channels = [&j](ChannelInput& c) {
        c = {};
        if (j.contains("left") || j.contains("right")) {
            c.left = convert<int>(required(j, "left"));
            c.right = convert<int>(required(j, "right"));
        } else {
            field(j, "channel", c.left);
        }
    };
    switch (kind) {
        case SourceKind::VirtualInput: {
            VirtualInputSpec v;
            channels(v);
            out.value = v;
            break;
        }
        case SourceKind::HardwareInput: {
            HardwareInputSpec h;
            channels(h);
            out.value = h;
            break;
        }
        case SourceKind::File: {
            FileInputSpec f;
            field(j, "path", f.path);
            if (auto it = j.find("relativeTo"); it != j.end()) {
                f.relativeToProject = convert<std::string>(*it) == "project";
            }
            field(j, "loop", f.loop);
            field(j, "gainDb", f.gainDb);
            field(j, "startSeconds", f.startSeconds);
            field(j, "playOnSceneRecall", f.playOnSceneRecall);
            out.value = f;
            break;
        }
        case SourceKind::Generator: {
            GeneratorSpec g;
            fieldEnum(j, "type", g.type, kGenerators);
            field(j, "hz", g.hz);
            field(j, "periodSeconds", g.periodSeconds);
            out.value = g;
            break;
        }
    }
    return out;
}

void to_json(Json& j, const SourceSpec& v) {
    j = Json{{"id", v.id},
             {"name", v.name},
             {"kind", enumName(kSourceKinds, v.kind())},
             {"input", inputToJson(v.input)},
             {"stereo", v.stereo},
             {"width", jf(v.width)},
             {"linkedPoints", v.linkedPoints},
             {"gainDb", jf(v.gainDb)},
             {"mute", v.mute},
             {"spatializer", enumName(kSpatializers, v.spatializer)},
             {"spread", jf(v.spread)},
             {"directSpeakers", v.directSpeakers},
             {"distance", v.distance},
             {"motion", v.motion},
             {"modulators", v.modulators},
             {"inserts", v.inserts}};
    if (v.motionRight) j["motionRight"] = *v.motionRight;
}

namespace {
template <class T>
void fieldObjVec(const Json& j, const char* key, std::vector<T>& out) {
    auto it = j.find(key);
    if (it == j.end()) return;
    if (!it->is_array()) fail(std::string(key) + ": expected an array");
    out.clear();
    std::size_t i = 0;
    for (const auto& e : *it) {
        try {
            out.push_back(e.get<T>());
        } catch (const std::exception& ex) {
            fail(std::string(key) + "[" + std::to_string(i) + "]: " + ex.what());
        }
        ++i;
    }
}
}  // namespace

void from_json(const Json& j, SourceSpec& v) {
    requireObject(j, "source");
    v = {};
    v.id = convert<int>(required(j, "id"));
    field(j, "name", v.name);
    SourceKind kind = SourceKind::VirtualInput;
    try {
        kind = enumValue(kSourceKinds, required(j, "kind"), "kind");
        if (auto it = j.find("input"); it != j.end())
            v.input = inputFromJson(kind, *it);
        else
            v.input = inputFromJson(kind, Json::object());
    } catch (const std::exception& e) {
        fail(e.what());
    }
    field(j, "stereo", v.stereo);
    field(j, "width", v.width);
    field(j, "linkedPoints", v.linkedPoints);
    field(j, "gainDb", v.gainDb);
    field(j, "mute", v.mute);
    fieldEnum(j, "spatializer", v.spatializer, kSpatializers);
    field(j, "spread", v.spread);
    fieldVec(j, "directSpeakers", v.directSpeakers);
    field(j, "distance", v.distance);
    field(j, "motion", v.motion);
    if (auto it = j.find("motionRight"); it != j.end()) {
        try {
            v.motionRight = it->get<MotionSpec>();
        } catch (const std::exception& e) {
            fail(std::string("motionRight: ") + e.what());
        }
    }
    fieldObjVec(j, "modulators", v.modulators);
    fieldObjVec(j, "inserts", v.inserts);
}

// ---- speaker chains / scenes ----------------------------------------------

void to_json(Json& j, const Eq3& v) {
    j = Json{{"lowFreq", jf(v.lowFreq)},      {"lowGainDb", jf(v.lowGainDb)}, {"midFreq", jf(v.midFreq)},
             {"midGainDb", jf(v.midGainDb)},  {"midQ", jf(v.midQ)},           {"highFreq", jf(v.highFreq)},
             {"highGainDb", jf(v.highGainDb)}};
}
void from_json(const Json& j, Eq3& v) {
    requireObject(j, "eq");
    v = {};
    field(j, "lowFreq", v.lowFreq);
    field(j, "lowGainDb", v.lowGainDb);
    field(j, "midFreq", v.midFreq);
    field(j, "midGainDb", v.midGainDb);
    field(j, "midQ", v.midQ);
    field(j, "highFreq", v.highFreq);
    field(j, "highGainDb", v.highGainDb);
}

void to_json(Json& j, const SpeakerChainSpec& v) {
    j = Json{{"speakerId", v.speakerId},
             {"trimDb", jf(v.trimDb)},
             {"userDelayMs", jf(v.userDelayMs)},
             {"polarityInverted", v.polarityInverted},
             {"mute", v.mute},
             {"eq", v.eq},
             {"inserts", v.inserts}};
}
void from_json(const Json& j, SpeakerChainSpec& v) {
    requireObject(j, "speaker chain");
    v = {};
    v.speakerId = convert<int>(required(j, "speakerId"));
    field(j, "trimDb", v.trimDb);
    field(j, "userDelayMs", v.userDelayMs);
    field(j, "polarityInverted", v.polarityInverted);
    field(j, "mute", v.mute);
    field(j, "eq", v.eq);
    fieldObjVec(j, "inserts", v.inserts);
}

void to_json(Json& j, const MasterSpec& v) {
    j = Json{{"gainDb", jf(v.gainDb)}};
}
void from_json(const Json& j, MasterSpec& v) {
    requireObject(j, "master");
    v = {};
    field(j, "gainDb", v.gainDb);
}

void to_json(Json& j, const Scene& v) {
    j = Json{
        {"name", v.name}, {"sources", v.sources}, {"speakerChains", v.speakerChains}, {"master", v.master}};
}
void from_json(const Json& j, Scene& v) {
    requireObject(j, "scene");
    v = {};
    field(j, "name", v.name);
    fieldObjVec(j, "sources", v.sources);
    fieldObjVec(j, "speakerChains", v.speakerChains);
    field(j, "master", v.master);
}

// ---- cues ------------------------------------------------------------------

namespace {
std::string formatTime(int hour, int minute) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%02d:%02d", hour, minute);
    return buf;
}
}  // namespace

void to_json(Json& j, const Trigger& v) {
    if (std::holds_alternative<trigger::Manual>(v.value)) {
        j = Json{{"kind", "Manual"}};
    } else if (const auto* atBeat = std::get_if<trigger::AtBeat>(&v.value)) {
        j = Json{{"kind", "AtBeat"}, {"bar", atBeat->bar}, {"beat", atBeat->beat}};
    } else if (const auto* after = std::get_if<trigger::AfterPrevious>(&v.value)) {
        j = Json{{"kind", "AfterPrevious"}, {"delay", after->delay}, {"randomRange", after->randomRange}};
    } else if (const auto* osc = std::get_if<trigger::Osc>(&v.value)) {
        j = Json{{"kind", "Osc"}, {"address", osc->address}};
    } else if (const auto* midi = std::get_if<trigger::Midi>(&v.value)) {
        j = Json{{"kind", "Midi"},
                 {"type", enumName(kTriggerMidiTypes, midi->type)},
                 {"number", midi->number},
                 {"channel", midi->channel}};
    } else {
        const auto& tod = std::get<trigger::TimeOfDay>(v.value);
        j = Json{{"kind", "TimeOfDay"}, {"time", formatTime(tod.hour, tod.minute)}, {"days", tod.days}};
    }
}
void from_json(const Json& j, Trigger& v) {
    requireObject(j, "trigger");
    const auto kind = convert<std::string>(required(j, "kind"));
    if (kind == "Manual") {
        v.value = trigger::Manual{};
    } else if (kind == "AtBeat") {
        trigger::AtBeat t;
        field(j, "bar", t.bar);
        field(j, "beat", t.beat);
        v.value = t;
    } else if (kind == "AfterPrevious") {
        trigger::AfterPrevious t;
        field(j, "delay", t.delay);
        field(j, "randomRange", t.randomRange);
        v.value = t;
    } else if (kind == "Osc") {
        trigger::Osc t;
        field(j, "address", t.address);
        v.value = t;
    } else if (kind == "Midi") {
        trigger::Midi t;
        fieldEnum(j, "type", t.type, kTriggerMidiTypes);
        field(j, "number", t.number);
        field(j, "channel", t.channel);
        v.value = t;
    } else if (kind == "TimeOfDay") {
        trigger::TimeOfDay t;
        std::string time;
        field(j, "time", time);
        if (!time.empty()) {
            int h = 0, m = 0, used = 0;
            if (std::sscanf(time.c_str(), "%d:%d%n", &h, &m, &used) != 2 ||
                static_cast<std::size_t>(used) != time.size() || h < 0 || h > 23 || m < 0 || m > 59)
                fail("time: expected \"HH:MM\", got '" + time + "'");
            t.hour = h;
            t.minute = m;
        }
        fieldVec(j, "days", t.days);
        v.value = t;
    } else {
        fail("kind: unknown trigger kind '" + kind + "'");
    }
}

void to_json(Json& j, const Cue& v) {
    j = Json{{"name", v.name},
             {"scene", v.sceneName},
             {"transitionSeconds", jf(v.transitionSeconds)},
             {"trigger", v.trigger},
             {"restartMotionPhase", v.restartMotionPhase},
             {"postWaitSeconds", jf(v.postWaitSeconds)}};
}
void from_json(const Json& j, Cue& v) {
    requireObject(j, "cue");
    v = {};
    field(j, "name", v.name);
    field(j, "scene", v.sceneName);
    field(j, "transitionSeconds", v.transitionSeconds);
    field(j, "trigger", v.trigger);
    field(j, "restartMotionPhase", v.restartMotionPhase);
    field(j, "postWaitSeconds", v.postWaitSeconds);
}

void to_json(Json& j, const CueList& v) {
    j = Json{{"name", v.name}, {"loop", v.loop}, {"cues", v.cues}};
}
void from_json(const Json& j, CueList& v) {
    requireObject(j, "cue list");
    v = {};
    field(j, "name", v.name);
    field(j, "loop", v.loop);
    fieldObjVec(j, "cues", v.cues);
}

// ---- project ---------------------------------------------------------------

void to_json(Json& j, const DeviceMapping& v) {
    j = Json{{"outputDeviceName", v.outputDeviceName},
             {"sampleRate", v.sampleRate},
             {"bufferSize", v.bufferSize},
             {"speedOfSound", jf(v.speedOfSound)}};
}
void from_json(const Json& j, DeviceMapping& v) {
    requireObject(j, "devices");
    v = {};
    field(j, "outputDeviceName", v.outputDeviceName);
    field(j, "sampleRate", v.sampleRate);
    field(j, "bufferSize", v.bufferSize);
    field(j, "speedOfSound", v.speedOfSound);
}

void to_json(Json& j, const AmbisonicsConfig& v) {
    j = Json{{"order", v.order}};
}
void from_json(const Json& j, AmbisonicsConfig& v) {
    requireObject(j, "ambisonics");
    v = {};
    field(j, "order", v.order);
}

void to_json(Json& j, const OscOutConfig& v) {
    j = Json{{"host", v.host}, {"port", v.port}, {"enabled", v.enabled}};
}
void from_json(const Json& j, OscOutConfig& v) {
    requireObject(j, "osc.out");
    v = {};
    field(j, "host", v.host);
    field(j, "port", v.port);
    field(j, "enabled", v.enabled);
}

void to_json(Json& j, const OscConfig& v) {
    j = Json{{"inPort", v.inPort}, {"out", v.out}};
}
void from_json(const Json& j, OscConfig& v) {
    requireObject(j, "osc");
    v = {};
    field(j, "inPort", v.inPort);
    field(j, "out", v.out);
}

void to_json(Json& j, const MidiMapping& v) {
    j = Json{{"type", enumName(kMidiTypes, v.type)},   {"channel", v.channel}, {"number", v.number},
             {"parameterPath", v.parameterPath},       {"min", jf(v.min)},     {"max", jf(v.max)},
             {"curve", enumName(kMidiCurves, v.curve)}};
}
void from_json(const Json& j, MidiMapping& v) {
    requireObject(j, "midi mapping");
    v = {};
    fieldEnum(j, "type", v.type, kMidiTypes);
    field(j, "channel", v.channel);
    field(j, "number", v.number);
    field(j, "parameterPath", v.parameterPath);
    field(j, "min", v.min);
    field(j, "max", v.max);
    fieldEnum(j, "curve", v.curve, kMidiCurves);
}

void to_json(Json& j, const MidiConfig& v) {
    j = Json{{"mappings", v.mappings}};
}
void from_json(const Json& j, MidiConfig& v) {
    requireObject(j, "midi");
    v = {};
    fieldObjVec(j, "mappings", v.mappings);
}

void to_json(Json& j, const ClockConfig& v) {
    j = Json{{"kind", enumName(kClockKinds, v.kind)},
             {"bpm", v.bpm},
             {"beatsPerBar", v.beatsPerBar},
             {"quantisedStart", v.quantisedStart}};
}
void from_json(const Json& j, ClockConfig& v) {
    requireObject(j, "clock");
    v = {};
    fieldEnum(j, "kind", v.kind, kClockKinds);
    field(j, "bpm", v.bpm);
    field(j, "beatsPerBar", v.beatsPerBar);
    field(j, "quantisedStart", v.quantisedStart);
}

void to_json(Json& j, const Project& v) {
    j = Json{{"schemaVersion", v.schemaVersion},
             {"name", v.name},
             {"layout", v.layout},
             {"devices", v.devices},
             {"ambisonics", v.ambisonics},
             {"sources", v.sources},
             {"speakerChains", v.speakerChains},
             {"master", v.master},
             {"scenes", v.scenes},
             {"cueLists", v.cueLists},
             {"osc", v.osc},
             {"midi", v.midi},
             {"clock", v.clock}};
}
void from_json(const Json& j, Project& v) {
    requireObject(j, "project");
    v = {};
    field(j, "schemaVersion", v.schemaVersion);
    field(j, "name", v.name);
    field(j, "layout", v.layout);
    field(j, "devices", v.devices);
    field(j, "ambisonics", v.ambisonics);
    fieldObjVec(j, "sources", v.sources);
    fieldObjVec(j, "speakerChains", v.speakerChains);
    field(j, "master", v.master);
    fieldObjVec(j, "scenes", v.scenes);
    fieldObjVec(j, "cueLists", v.cueLists);
    field(j, "osc", v.osc);
    field(j, "midi", v.midi);
    field(j, "clock", v.clock);
}

// ---- unknown keys ----------------------------------------------------------

namespace {
void collectUnknown(const Json& in, const Json& canon, const std::string& path,
                    std::vector<std::string>& out) {
    if (in.is_object() && canon.is_object()) {
        for (auto it = in.begin(); it != in.end(); ++it) {
            const std::string child = path.empty() ? it.key() : path + "." + it.key();
            auto c = canon.find(it.key());
            if (c == canon.end()) {
                out.push_back(child);
            } else {
                collectUnknown(it.value(), *c, child, out);
            }
        }
    } else if (in.is_array() && canon.is_array()) {
        const auto n = std::min(in.size(), canon.size());
        for (std::size_t i = 0; i < n; ++i)
            collectUnknown(in[i], canon[i], path + "[" + std::to_string(i) + "]", out);
    }
}
}  // namespace

std::vector<std::string> findUnknownKeys(const Json& input, const Json& canonical) {
    std::vector<std::string> out;
    collectUnknown(input, canonical, "", out);
    return out;
}

}  // namespace spacer
