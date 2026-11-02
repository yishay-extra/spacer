#include "core/motion/ModulatorBank.h"

#include <algorithm>
#include <variant>

#include "core/motion/Lfo.h"
#include "core/motion/Trajectory.h"

namespace spacer {

void ModulatorBank::configure(const std::vector<ModulatorSpec>& specs) {
    count_ = static_cast<int>(std::min<std::size_t>(specs.size(), slots_.size()));
    for (int i = 0; i < count_; ++i) {
        const ModulatorSpec& m = specs[static_cast<std::size_t>(i)];
        Slot s;
        if (const auto* lfo = std::get_if<LfoSource>(&m.source)) {
            s.kind = Kind::Lfo;
            s.shape = lfo->shape;
            s.period = lfo->period;
        } else {
            s.kind = Kind::External;
        }
        const auto path = ParameterPath::parse(m.target);
        s.valid = path.has_value();
        if (path) s.target = *path;
        s.depth = m.depth;
        s.offset = m.offset;
        slots_[static_cast<std::size_t>(i)] = s;
    }
    timeline_.restart();
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
    Output out;
    for (int i = 0; i < count_; ++i) {
        Slot& s = slots_[static_cast<std::size_t>(i)];
        const float src = s.kind == Kind::Lfo
                              ? lfoValue(s.shape, cyclesOf(t, s.period), static_cast<std::uint32_t>(i + 1))
                              : s.external;
        s.last = s.offset + s.depth * src;
        if (!s.valid) continue;
        if (s.target.domain == ParameterPath::Domain::Source)
            out.source[s.target.source] += s.last;
        else
            out.motion[s.target.motion] += s.last;
    }
    return out;
}

SourceModulation ModulatorBank::applyTo(Trajectory& motion, const MotionContext& ctx) {
    const Output out = process(ctx);
    motion.setModulation(out.motion);
    return out.source;
}

}  // namespace spacer
