#include "control/ControlHub.h"

namespace spacer {

namespace {
CueTick makeTick(const EngineProcessor& engine) {
    CueTick t;
    t.nowSeconds = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    t.clock = engine.clockState();
    const auto now = juce::Time::getCurrentTime();
    t.wall.minuteStamp = now.toMilliseconds() / 60000;
    t.wall.dayOfWeek = now.getDayOfWeek();
    t.wall.hour = now.getHours();
    t.wall.minute = now.getMinutes();
    return t;
}
}  // namespace

ControlHub::ControlHub(const Project& project, EngineProcessor& engine, CueEngine* cues,
                       ControlDispatcher::Config config)
    : project_(project), dispatcher_(project, cues, completeConfig(engine, std::move(config))) {}

ControlDispatcher::Config ControlHub::completeConfig(EngineProcessor& engine,
                                                     ControlDispatcher::Config config) {
    if (!config.push) config.push = [&engine](const Command& c) { return engine.push(c); };
    if (!config.makeTick) config.makeTick = [&engine] { return makeTick(engine); };
    if (!config.pushAt)
        config.pushAt = [this, &engine](const Command& c, double dueEpochSeconds) {
            // Timetag (epoch) -> host time -> engine sample, via the pump's own clocks.
            const double host = nowMonotonic_ + (dueEpochSeconds - nowEpoch_);
            const std::int64_t sample = engine.sampleAtHostTime(host);
            return sample >= 0 ? engine.pushAt(c, sample) : engine.push(c);
        };
    return config;
}

bool ControlHub::applyProject() {
    mapper_.setConfig(project_.midi);
    bool ok = server_.start(project_.osc.inPort);
    if (project_.osc.out.enabled)
        sender_.connect(project_.osc.out.host, project_.osc.out.port);
    else
        sender_.disconnect();
    return ok;
}

void ControlHub::onFeedback(const Feedback& feedback) {
    sender_.onFeedback(feedback);
}

void ControlHub::pump() {
    pump(static_cast<double>(juce::Time::currentTimeMillis()) / 1000.0,
         juce::Time::getMillisecondCounterHiRes() / 1000.0);
}

void ControlHub::pump(double nowEpochSeconds, double monotonicSeconds) {
    nowEpoch_ = nowEpochSeconds;
    nowMonotonic_ = monotonicSeconds;
    server_.pump(nowEpochSeconds, kTimetagLookaheadSeconds,
                 [this](const ControlEvent& e, double due) { dispatcher_.dispatch(e, due); });

    midiScratch_.clear();
    midiInbox_.drainTo(midiScratch_);
    for (const auto& m : midiScratch_) {
        events_.clear();
        mapper_.handle(m, events_);
        for (const auto& e : events_) dispatcher_.dispatch(e);
    }

    sender_.tick(monotonicSeconds);
}

}  // namespace spacer
