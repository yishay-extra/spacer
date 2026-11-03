#include "core/motion/ModulatorBank.h"

#include <algorithm>
#include <variant>

#include "core/motion/Lfo.h"
#include "core/motion/Trajectory.h"

namespace spacer {

ModulatorBank::Live ModulatorBank::Live::fromSpec(const ModulatorSpec& m) {
    Live l;
    if (const auto* lfo = std::get_if<LfoSource>(&m.source)) {
        l.lfo = true;
        l.shape = lfo->shape;
        l.period = lfo->period;
    } else {
        l.lfo = false;
    }
    const auto path = ParameterPath::parse(m.target);
    l.targetValid = path.has_value();
    if (path) l.target = *path;
    l.depth = m.depth;
    l.offset = m.offset;
    return l;
}

ModulatorBank::Slot ModulatorBank::slotFrom(const Live& m) {
    Slot s;
    s.kind = m.lfo ? Kind::Lfo : Kind::External;
    s.shape = m.shape;
    s.period = m.period;
    s.valid = m.targetValid;
    s.target = m.target;
    s.depth = m.depth;
    s.offset = m.offset;
    return s;
}

void ModulatorBank::configure(const std::vector<ModulatorSpec>& specs) {
    count_ = static_cast<int>(std::min<std::size_t>(specs.size(), slots_.size()));
    for (int i = 0; i < count_; ++i)
        slots_[static_cast<std::size_t>(i)] = slotFrom(Live::fromSpec(specs[static_cast<std::size_t>(i)]));
    reset();
}

void ModulatorBank::reset() {
    timeline_.restart();
    last_ = {};
    for (auto& s : slots_) s.cycleOffset = 0.0;
}

double ModulatorBank::continuedOffset(const Slot& prev, const Slot& next) const {
    if (prev.kind != Kind::Lfo || next.kind != Kind::Lfo) return 0.0;
    const double phase = cyclesOf(last_, prev.period) + prev.cycleOffset;
    return phase - cyclesOf(last_, next.period);
}

void ModulatorBank::continueTimelineFrom(const ModulatorBank& other) {
    timeline_ = other.timeline_;
    last_ = other.last_;
    for (int i = 0; i < count_; ++i) {
        Slot& s = slots_[static_cast<std::size_t>(i)];
        if (i >= other.count_) {
            s.cycleOffset = 0.0;
            continue;
        }
        const Slot& o = other.slots_[static_cast<std::size_t>(i)];
        s.cycleOffset = continuedOffset(o, s);
        if (s.kind == Kind::External && o.kind == Kind::External) s.external = o.external;
    }
}

void ModulatorBank::setLive(int index, const Live& m, int count) {
    if (index < 0) {  // only truncate
        count_ = std::clamp(count, 0, count_);
        return;
    }
    if (index >= kMaxModulators || index > count_) return;
    Slot next = slotFrom(m);
    if (index < count_) {
        const Slot& prev = slots_[static_cast<std::size_t>(index)];
        next.cycleOffset = continuedOffset(prev, next);
        if (prev.kind == Kind::External && next.kind == Kind::External) next.external = prev.external;
        next.last = prev.last;
    } else if (next.kind == Kind::Lfo) {
        next.cycleOffset = -cyclesOf(last_, next.period);  // phase 0 now
    }
    slots_[static_cast<std::size_t>(index)] = next;
    count_ = std::clamp(std::max(count, index + 1), 0, kMaxModulators);
}

bool ModulatorBank::targets(SourceParam p) const {
    for (int i = 0; i < count_; ++i) {
        const Slot& s = slots_[static_cast<std::size_t>(i)];
        if (s.valid && s.target.domain == ParameterPath::Domain::Source && s.target.source == p) return true;
    }
    return false;
}

void ModulatorBank::setExternal(int index, float value) {
    if (index < 0 || index >= count_) return;
    slots_[static_cast<std::size_t>(index)].external = value;
}

bool ModulatorBank::active(int index) const {
    return index >= 0 && index < count_ && slots_[static_cast<std::size_t>(index)].valid;
}

float ModulatorBank::value(int index) const {
    return (index >= 0 && index < count_) ? slots_[static_cast<std::size_t>(index)].last : 0.0f;
}

ModulatorBank::Output ModulatorBank::process(const MotionContext& ctx) {
    const MotionTime t = timeline_.advance(ctx);
    last_ = t;
    Output out;
    for (int i = 0; i < count_; ++i) {
        Slot& s = slots_[static_cast<std::size_t>(i)];
        const float src = s.kind == Kind::Lfo ? lfoValue(s.shape, cyclesOf(t, s.period) + s.cycleOffset,
                                                         static_cast<std::uint32_t>(i + 1))
                                              : s.external;
        s.last = s.offset + s.depth * src;
        if (!s.valid) continue;
        switch (s.target.domain) {
            case ParameterPath::Domain::Source:
                out.source[s.target.source] += s.last;
                break;
            case ParameterPath::Domain::Motion:
                out.motion[s.target.motion] += s.last;
                break;
            case ParameterPath::Domain::MotionRight:
                out.motionRight[s.target.motion] += s.last;
                break;
            case ParameterPath::Domain::Insert: {
                int k = 0;
                while (k < out.numInserts &&
                       (out.inserts[static_cast<std::size_t>(k)].slot != s.target.slot ||
                        out.inserts[static_cast<std::size_t>(k)].index != s.target.index))
                    ++k;
                auto& o = out.inserts[static_cast<std::size_t>(k)];
                if (k == out.numInserts) {
                    o = {s.target.slot, s.target.index, 0.0f};
                    ++out.numInserts;
                }
                o.value += s.last;
                break;
            }
        }
    }
    return out;
}

SourceModulation ModulatorBank::applyTo(Trajectory& motion, const MotionContext& ctx) {
    const Output out = process(ctx);
    motion.setModulation(out.motion);
    return out.source;
}

}  // namespace spacer
