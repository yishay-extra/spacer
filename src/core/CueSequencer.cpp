#include "core/CueSequencer.h"

#include <algorithm>
#include <utility>
#include <variant>

namespace spacer {

CueSequencer::CueSequencer(CueList list, std::uint64_t seed) : list_(std::move(list)), rng_(seed) {}

void CueSequencer::setList(CueList list) {
    list_ = std::move(list);
    current_ = -1;
    deadlineActive_ = false;
}

void CueSequencer::arm(const CueTick& tick) {
    current_ = -1;
    postWaitEnd_ = tick.nowSeconds;
    scheduleNext(tick);
}

int CueSequencer::next() const noexcept {
    const int n = static_cast<int>(list_.cues.size());
    if (n == 0) return -1;
    if (current_ + 1 < n) return current_ + 1;
    return list_.loop ? 0 : -1;
}

double CueSequencer::beatPosition(const trigger::AtBeat& t, int beatsPerBar) {
    return static_cast<double>(t.bar - 1) * std::max(1, beatsPerBar) + (t.beat - 1.0);
}

void CueSequencer::scheduleNext(const CueTick& tick) {
    deadlineActive_ = false;
    const int n = next();
    if (n < 0) return;
    const auto* after =
        std::get_if<trigger::AfterPrevious>(&list_.cues[static_cast<std::size_t>(n)].trigger.value);
    if (after == nullptr) return;
    const double extra = after->randomRange > 0.0 ? rng_.uniform() * after->randomRange : 0.0;
    const double delay = std::max(0.0, after->delay.value) + extra;
    deadlineActive_ = true;
    deadlineInBeats_ = after->delay.unit == Period::Unit::Beats;
    deadlineAt_ = (deadlineInBeats_ ? tick.clock.beat : tick.nowSeconds) + delay;
}

std::optional<int> CueSequencer::start(int index, const CueTick& tick) {
    if (index < 0 || index >= static_cast<int>(list_.cues.size())) return std::nullopt;
    current_ = index;
    postWaitEnd_ =
        tick.nowSeconds + static_cast<double>(list_.cues[static_cast<std::size_t>(index)].postWaitSeconds);
    scheduleNext(tick);
    return index;
}

std::optional<int> CueSequencer::go(const CueTick& tick) {
    const int n = next();
    return n < 0 ? std::nullopt : start(n, tick);
}

std::optional<int> CueSequencer::back(const CueTick& tick) {
    if (current_ < 0) return std::nullopt;
    int target = current_ - 1;
    if (target < 0) {
        if (!list_.loop) return std::nullopt;
        target = static_cast<int>(list_.cues.size()) - 1;
    }
    return start(target, tick);
}

std::optional<int> CueSequencer::jump(int index, const CueTick& tick) {
    return start(index, tick);
}

std::optional<int> CueSequencer::update(const CueTick& tick) {
    const int n = next();
    if (n < 0) return std::nullopt;
    const Cue& cue = list_.cues[static_cast<std::size_t>(n)];

    if (std::holds_alternative<trigger::AfterPrevious>(cue.trigger.value)) {
        if (!deadlineActive_ || tick.nowSeconds < postWaitEnd_) return std::nullopt;
        const bool due = deadlineInBeats_ ? (tick.clock.playing && tick.clock.beat >= deadlineAt_)
                                          : tick.nowSeconds >= deadlineAt_;
        return due ? start(n, tick) : std::nullopt;
    }
    if (const auto* tod = std::get_if<trigger::TimeOfDay>(&cue.trigger.value)) {
        if (tick.wall.hour != tod->hour || tick.wall.minute != tod->minute) return std::nullopt;
        if (tick.wall.minuteStamp == lastTimeOfDayStamp_) return std::nullopt;
        if (!tod->days.empty() &&
            std::find(tod->days.begin(), tod->days.end(), tick.wall.dayOfWeek) == tod->days.end())
            return std::nullopt;
        lastTimeOfDayStamp_ = tick.wall.minuteStamp;
        return start(n, tick);
    }
    return std::nullopt;
}

std::optional<int> CueSequencer::onOsc(const std::string& address, const CueTick& tick) {
    const int n = next();
    if (n < 0) return std::nullopt;
    const auto* osc = std::get_if<trigger::Osc>(&list_.cues[static_cast<std::size_t>(n)].trigger.value);
    if (osc == nullptr || osc->address != address) return std::nullopt;
    return start(n, tick);
}

std::optional<int> CueSequencer::onMidi(trigger::Midi::Type type, int number, int channel,
                                        const CueTick& tick) {
    const int n = next();
    if (n < 0) return std::nullopt;
    const auto* midi = std::get_if<trigger::Midi>(&list_.cues[static_cast<std::size_t>(n)].trigger.value);
    if (midi == nullptr || midi->type != type || midi->number != number) return std::nullopt;
    if (midi->channel != 0 && midi->channel != channel) return std::nullopt;
    return start(n, tick);
}

std::optional<CueSequencer::BeatTrigger> CueSequencer::pendingBeatTrigger(int beatsPerBar) const {
    const int n = next();
    if (n < 0) return std::nullopt;
    const auto* at = std::get_if<trigger::AtBeat>(&list_.cues[static_cast<std::size_t>(n)].trigger.value);
    if (at == nullptr) return std::nullopt;
    return BeatTrigger{n, beatPosition(*at, beatsPerBar)};
}

std::optional<int> CueSequencer::beatFired(int cueIndex, const CueTick& tick) {
    if (cueIndex != next()) return std::nullopt;
    return start(cueIndex, tick);
}

}  // namespace spacer
