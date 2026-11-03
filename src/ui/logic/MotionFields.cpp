#include "ui/logic/MotionFields.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <functional>
#include <type_traits>
#include <variant>

#include "ui/logic/FieldBuilder.h"

namespace spacer {

namespace {

using fmt::Unit;
using namespace fields;

const std::vector<std::string> kPlayModes{"Once", "Loop", "Ping-pong"};

void buildManual(Builder& b, ManualMotion& m) {
    b.position("position", "Position", m.position,
               std::array{MotionParam::PositionAz, MotionParam::PositionEl, MotionParam::PositionDist});
    b.group("");
    b.number("smoothMs", "Smoothing", Unit::Millis, 0.0f, 5000.0f, m.smoothMs);
}

void buildOrbit(Builder& b, OrbitMotion& m) {
    b.position("centre", "Centre", m.centre,
               std::array{MotionParam::CentreAz, MotionParam::CentreEl, MotionParam::CentreDist});
    b.group("Orbit");
    b.number("radius", "Radius", Unit::Metres, 0.0f, 100.0f, m.radius, MotionParam::Radius);
    b.period("period", "Period", m.period);
    b.choice("direction", "Direction", {"Clockwise", "Counter-clockwise"}, m.direction >= 0 ? 0 : 1,
             [&m](int i) {
                 m.direction = i == 0 ? 1 : -1;
                 return true;
             });
    b.number("phaseOffset", "Phase offset", Unit::Degrees, -360.0f, 360.0f, m.phaseOffset,
             MotionParam::PhaseOffset);
    b.number("elevationWobble", "Elevation wobble", Unit::Degrees, 0.0f, 90.0f, m.elevationWobble,
             MotionParam::ElevationWobble);
    b.period("wobblePeriod", "Wobble period", m.wobblePeriod);
}

void buildLine(Builder& b, LineMotion& m) {
    b.position("from", "From", m.from,
               std::array{MotionParam::FromAz, MotionParam::FromEl, MotionParam::FromDist});
    b.position("to", "To", m.to, std::array{MotionParam::ToAz, MotionParam::ToEl, MotionParam::ToDist});
    b.group("Line");
    b.period("period", "Period", m.period);
    b.enumChoice("mode", "Mode", kPlayModes, m.mode);
    b.enumChoice("easing", "Easing", {"Linear", "Smooth"}, m.easing);
}

void buildLissajous(Builder& b, LissajousMotion& m) {
    b.position("centre", "Centre", m.centre,
               std::array{MotionParam::CentreAz, MotionParam::CentreEl, MotionParam::CentreDist});
    b.group("Amplitude");
    b.number("ax", "X amplitude", Unit::Metres, 0.0f, 100.0f, m.ax, MotionParam::Ax);
    b.number("ay", "Y amplitude", Unit::Metres, 0.0f, 100.0f, m.ay, MotionParam::Ay);
    b.number("az", "Z amplitude", Unit::Metres, 0.0f, 100.0f, m.az, MotionParam::Az);
    b.group("Frequency (cycles per period)");
    b.number("fx", "X", Unit::None, 0.0f, 32.0f, m.fx);
    b.number("fy", "Y", Unit::None, 0.0f, 32.0f, m.fy);
    b.number("fz", "Z", Unit::None, 0.0f, 32.0f, m.fz);
    b.group("Phase");
    b.number("phases.x", "X", Unit::Degrees, -360.0f, 360.0f, m.phases[0], MotionParam::PhaseX);
    b.number("phases.y", "Y", Unit::Degrees, -360.0f, 360.0f, m.phases[1], MotionParam::PhaseY);
    b.number("phases.z", "Z", Unit::Degrees, -360.0f, 360.0f, m.phases[2], MotionParam::PhaseZ);
    b.group("");
    b.period("period", "Period", m.period);
}

void buildPath(Builder& b, PathMotion& m) {
    b.group("Path");
    b.period("period", "Period", m.period);
    b.enumChoice("mode", "Mode", kPlayModes, m.mode);
    b.enumChoice("interpolation", "Interpolation", {"Linear", "Catmull-Rom"}, m.interpolation);
    b.toggle("constantSpeed", "Constant speed", m.constantSpeed);
    for (std::size_t i = 0; i < m.points.size(); ++i) {
        const std::string n = std::to_string(i);
        b.group("Point " + std::to_string(i + 1), static_cast<int>(i));
        b.setPrefix("points." + n + ".");
        b.number("x", "X (right)", Unit::Metres, -100.0f, 100.0f, m.points[i].x);
        b.number("y", "Y (front)", Unit::Metres, -100.0f, 100.0f, m.points[i].y);
        b.number("z", "Z (up)", Unit::Metres, -100.0f, 100.0f, m.points[i].z);
        b.setPrefix("");
    }
}

void buildRandomWalk(Builder& b, RandomWalkMotion& m) {
    b.group("Walk");
    b.number("stepMetres", "Step size", Unit::Metres, 0.0f, 50.0f, m.stepMetres, MotionParam::StepMetres);
    b.period("stepPeriod", "Step period", m.stepPeriod);
    b.toggle("glide", "Glide", m.glide);
    b.seed("seed", m.seed);
    b.group("Bounds");
    const bool box = std::holds_alternative<BoxBounds>(m.bounds);
    b.choice("bounds", "Shape", {"Box", "Sphere"}, box ? 0 : 1, [&m](int i) {
        if (i == 0 && std::holds_alternative<SphereBounds>(m.bounds)) {
            const auto s = std::get<SphereBounds>(m.bounds);
            m.bounds = BoxBounds{s.centre - Cartesian{s.radius, s.radius, s.radius},
                                 s.centre + Cartesian{s.radius, s.radius, s.radius}};
        } else if (i == 1 && std::holds_alternative<BoxBounds>(m.bounds)) {
            const auto bx = std::get<BoxBounds>(m.bounds);
            const Cartesian c = (bx.min + bx.max) * 0.5f;
            const Cartesian h = (bx.max - bx.min) * 0.5f;
            m.bounds = SphereBounds{c, std::max({h.x, h.y, h.z, 0.1f})};
        }
        return true;
    });
    if (auto* bx = std::get_if<BoxBounds>(&m.bounds)) {
        b.cartesian("box.min", "Minimum corner", bx->min);
        b.cartesian("box.max", "Maximum corner", bx->max);
    } else {
        auto& s = std::get<SphereBounds>(m.bounds);
        b.cartesian("sphere.centre", "Centre", s.centre);
        b.group("Radius");
        b.number("sphere.radius", "Radius", Unit::Metres, 0.1f, 100.0f, s.radius);
    }
}

void buildSteps(Builder& b, StepSequenceMotion& m) {
    const Layout* layout = b.layout();
    b.group("Sequence");
    b.enumChoice("order", "Order", {"Forward", "Ping-pong", "Random"}, m.order);
    b.toggle("loop", "Loop", m.loop);
    b.seed("seed", m.seed);
    for (std::size_t i = 0; i < m.steps.size(); ++i) {
        auto& st = m.steps[i];
        const int item = static_cast<int>(i);
        const std::string n = std::to_string(i);
        b.group("Step " + std::to_string(i + 1), item);
        b.setPrefix("steps." + n + ".");
        const bool toSpeaker = std::holds_alternative<int>(st.target);
        b.choice("target", "Target", {"Position", "Speaker"}, toSpeaker ? 1 : 0, [&st, layout](int k) {
            if (k == 1 && std::holds_alternative<Position>(st.target)) {
                int id = 1;
                if (layout != nullptr && !layout->speakers.empty()) id = layout->speakers.front().id;
                st.target = id;
            } else if (k == 0 && std::holds_alternative<int>(st.target)) {
                Position p{};
                if (layout != nullptr)
                    if (const auto* sp = layout->findSpeaker(std::get<int>(st.target))) p = sp->position;
                st.target = p;
            }
            return true;
        });
        if (auto* pos = std::get_if<Position>(&st.target)) {
            b.position("", "Position", *pos, std::nullopt, false);
        } else {
            b.integer("speaker", "Speaker id", 1, 9999, std::get<int>(st.target));
        }
        b.period("duration", "Duration", st.duration);
        b.enumChoice("transition", "Transition", {"Jump", "Glide"}, st.transition);
        if (st.transition == StepTransition::Glide) b.period("glideTime", "Glide time", st.glideTime);
        b.number("probability", "Probability", Unit::Percent, 0.0f, 1.0f, st.probability);
        b.setPrefix("");
    }
}

std::vector<Binding> bindMotion(MotionSpec& spec, const Layout* layout) {
    Builder b(layout);
    std::visit(
        [&b](auto& m) {
            using T = std::decay_t<decltype(m)>;
            if constexpr (std::is_same_v<T, ManualMotion>)
                buildManual(b, m);
            else if constexpr (std::is_same_v<T, OrbitMotion>)
                buildOrbit(b, m);
            else if constexpr (std::is_same_v<T, LineMotion>)
                buildLine(b, m);
            else if constexpr (std::is_same_v<T, LissajousMotion>)
                buildLissajous(b, m);
            else if constexpr (std::is_same_v<T, PathMotion>)
                buildPath(b, m);
            else if constexpr (std::is_same_v<T, RandomWalkMotion>)
                buildRandomWalk(b, m);
            else
                buildSteps(b, m);
        },
        spec.value);
    return b.take();
}

}  // namespace

MotionKind motionKindOf(const MotionSpec& spec) {
    return static_cast<MotionKind>(spec.value.index());
}

const char* motionKindName(MotionKind kind) {
    switch (kind) {
        case MotionKind::Manual:
            return "Manual";
        case MotionKind::Orbit:
            return "Orbit";
        case MotionKind::Line:
            return "Line";
        case MotionKind::Lissajous:
            return "Lissajous";
        case MotionKind::Path:
            return "Path";
        case MotionKind::RandomWalk:
            return "Random walk";
        case MotionKind::StepSequence:
            return "Step sequence";
    }
    return "";
}

std::vector<MotionKind> allMotionKinds() {
    return {MotionKind::Manual, MotionKind::Orbit,      MotionKind::Line,        MotionKind::Lissajous,
            MotionKind::Path,   MotionKind::RandomWalk, MotionKind::StepSequence};
}

MotionSpec makeMotion(MotionKind kind, const Position& startPoint) {
    MotionSpec m;
    switch (kind) {
        case MotionKind::Manual:
            m.value = ManualMotion{startPoint, 20.0f};
            break;
        case MotionKind::Orbit:
            m.value = OrbitMotion{};
            break;
        case MotionKind::Line: {
            LineMotion l;
            l.from = startPoint;
            m.value = l;
            break;
        }
        case MotionKind::Lissajous:
            m.value = LissajousMotion{};
            break;
        case MotionKind::Path: {
            PathMotion p;
            p.points = {{-1.0f, 2.0f, 0.0f}, {1.0f, 2.0f, 0.0f}};
            m.value = p;
            break;
        }
        case MotionKind::RandomWalk:
            m.value = RandomWalkMotion{};
            break;
        case MotionKind::StepSequence: {
            StepSequenceMotion s;
            s.steps = {Step{Position{-45.0f, 0.0f, 2.0f}}, Step{Position{45.0f, 0.0f, 2.0f}}};
            m.value = s;
            break;
        }
    }
    return m;
}

std::vector<MotionField> motionFields(const MotionSpec& spec) {
    MotionSpec copy = spec;
    auto bindings = bindMotion(copy, nullptr);
    std::vector<MotionField> out;
    out.reserve(bindings.size());
    for (auto& b : bindings) out.push_back(std::move(b.field));
    return out;
}

std::optional<MotionField> findMotionField(const MotionSpec& spec, const std::string& fieldId) {
    for (auto& f : motionFields(spec))
        if (f.id == fieldId) return f;
    return std::nullopt;
}

bool setMotionField(MotionSpec& spec, const Layout& layout, const std::string& fieldId,
                    const std::string& text) {
    // Work on a copy so a rejected value leaves `spec` untouched.
    MotionSpec work = spec;
    auto bindings = bindMotion(work, &layout);
    for (auto& b : bindings) {
        if (b.field.id != fieldId) continue;
        const bool ok = applyText(b, text);
        if (ok) spec = work;
        return ok;
    }
    return false;
}

std::optional<MotionParam> liveParamOf(const MotionSpec& spec, const std::string& fieldId) {
    if (motionKindOf(spec) == MotionKind::Manual) return std::nullopt;
    const auto f = findMotionField(spec, fieldId);
    if (!f || f->type != FieldType::Number) return std::nullopt;
    return f->param;
}

bool setMotionParamValue(MotionSpec& spec, MotionParam param, float value) {
    if (!std::isfinite(value)) return false;
    for (auto& f : motionFields(spec))
        if (f.param && *f.param == param) {
            return setMotionField(spec, Layout{}, f.id, fmt::number(value, 6));
        }
    return false;
}

std::string motionItemName(const MotionSpec& spec) {
    if (std::holds_alternative<PathMotion>(spec.value)) return "Point";
    if (std::holds_alternative<StepSequenceMotion>(spec.value)) return "Step";
    return {};
}

int motionItemCount(const MotionSpec& spec) {
    if (const auto* p = std::get_if<PathMotion>(&spec.value)) return static_cast<int>(p->points.size());
    if (const auto* s = std::get_if<StepSequenceMotion>(&spec.value))
        return static_cast<int>(s->steps.size());
    return 0;
}

int motionItemMinimum(const MotionSpec& spec) {
    if (std::holds_alternative<PathMotion>(spec.value)) return 2;
    if (std::holds_alternative<StepSequenceMotion>(spec.value)) return 1;
    return 0;
}

bool addMotionItem(MotionSpec& spec) {
    if (auto* p = std::get_if<PathMotion>(&spec.value)) {
        Cartesian next = p->points.empty() ? Cartesian{} : p->points.back();
        next.x += 0.5f;
        p->points.push_back(next);
        return true;
    }
    if (auto* s = std::get_if<StepSequenceMotion>(&spec.value)) {
        s->steps.push_back(s->steps.empty() ? Step{} : s->steps.back());
        return true;
    }
    return false;
}

bool removeMotionItem(MotionSpec& spec, int index) {
    if (index < 0 || motionItemCount(spec) <= motionItemMinimum(spec) || index >= motionItemCount(spec))
        return false;
    if (auto* p = std::get_if<PathMotion>(&spec.value))
        p->points.erase(p->points.begin() + index);
    else if (auto* s = std::get_if<StepSequenceMotion>(&spec.value))
        s->steps.erase(s->steps.begin() + index);
    return true;
}

}  // namespace spacer
