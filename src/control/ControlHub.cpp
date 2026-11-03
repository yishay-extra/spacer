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

ControlHub::ControlHub(const Project& project, EngineProcessor& engine, CueEngine* cues)
    : project_(project),
      dispatcher_(project, cues,
                  ControlDispatcher::Config{[&engine](const Command& c) { return engine.push(c); },
                                            [&engine] { return makeTick(engine); },
                                            {},
                                            {},
                                            {}}) {}

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
    server_.pump(nowEpochSeconds, [this](const ControlEvent& e) { dispatcher_.dispatch(e); });

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
