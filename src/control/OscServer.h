#pragma once

#include <juce_osc/juce_osc.h>

#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <vector>

#include "control/ControlEvent.h"
#include "control/ControlInbox.h"

namespace spacer {

/// OSC input (SPEC §12.1): UDP receiver on its own thread, routed through
/// OscRouter into ControlEvents, handed to the message thread through a
/// ControlInbox. The receiver thread never touches the engine.
///
/// Bundle timetags are honoured: an event inside a bundle with a future
/// timetag is held on the message thread and delivered by pump() once due.
/// With the look-ahead pump, events that become engine commands are handed
/// over up to `lookaheadSeconds` early together with their timetag, so the
/// engine can apply them at the exact sample (EngineProcessor::pushAt).
class OscServer : private juce::OSCReceiver::Listener<juce::OSCReceiver::RealtimeCallback> {
public:
    struct Stats {
        std::uint64_t messagesReceived{0};
        std::uint64_t malformedPackets{0};   // not parseable as OSC at all
        std::uint64_t malformedMessages{0};  // known address, wrong arguments
        std::uint64_t dropped{0};            // inbox / schedule full
    };

    OscServer();
    ~OscServer() override;

    /// Binds UDP `port`. False if it cannot be bound.
    bool start(int port);
    void stop();
    bool running() const noexcept { return running_; }

    /// Message thread. Takes in what the receiver queued, then calls
    /// `deliver` for every event that is due at `nowEpochSeconds` (seconds
    /// since 1970), in timetag order, then arrival order.
    void pump(double nowEpochSeconds, const std::function<void(const ControlEvent&)>& deliver);
    /// As above, but engine events (everything except cues, scenes and cue
    /// triggers) due within `lookaheadSeconds` are delivered early.
    /// `dueEpochSeconds` is the event's timetag, 0 for an immediate message.
    void pump(double nowEpochSeconds, double lookaheadSeconds,
              const std::function<void(const ControlEvent&, double dueEpochSeconds)>& deliver);

    Stats stats() const noexcept;

    /// Receiver-thread hook for OscClock (WP12): `/spacer/clock/beat` and
    /// `/spacer/clock/stop` are offered with their host time (seconds on
    /// juce::Time::getMillisecondCounterHiRes() / 1000: arrival time, or the
    /// bundle timetag mapped onto that clock) before they are queued for the
    /// message thread. Returning true consumes the event. Called on the
    /// receiver thread: it must only hand the data over (ClockInputs).
    using ClockTap = std::function<bool(const ControlEvent& event, double hostTime)>;
    void setClockTap(ClockTap tap);

    // The receiver thread's entry points, public so tests can feed synthetic
    // packets without a socket.
    void handleMessage(const juce::OSCMessage& message, double dueEpochSeconds = 0.0);
    void handleBundle(const juce::OSCBundle& bundle);
    void handleMalformedPacket() noexcept { malformedPackets_.fetch_add(1, std::memory_order_relaxed); }

private:
    struct Timed {
        double due{0.0};
        ControlEvent event;
    };
    static constexpr std::size_t kMaxScheduled = 4096;

    void oscMessageReceived(const juce::OSCMessage& message) override;
    void oscBundleReceived(const juce::OSCBundle& bundle) override;
    void handleBundle(const juce::OSCBundle& bundle, int depth);

    std::mutex clockTapMutex_;  // receiver thread vs. setClockTap (never the audio thread)
    ClockTap clockTap_;
    juce::OSCReceiver receiver_;
    bool running_{false};
    ControlInbox<Timed> inbox_;
    std::multimap<double, ControlEvent> scheduled_;  // message thread only
    std::vector<Timed> scratch_;
    std::atomic<std::uint64_t> received_{0}, malformedPackets_{0}, malformedMessages_{0};
    std::atomic<std::uint64_t> scheduleDropped_{0};
};

}  // namespace spacer
