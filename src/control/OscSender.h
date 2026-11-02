#pragma once

// OSC feedback out (SPEC §12.1 "Out, optional, 30 Hz"). The app feeds it the
// engine's Feedback stream and the current cue; tick() on a message-thread
// timer sends one bundle per 1/30 s containing what changed.

#include <juce_osc/juce_osc.h>

#include <functional>
#include <map>
#include <optional>
#include <string>

#include "engine/Feedback.h"

namespace spacer {

class OscSender {
public:
    static constexpr double kRateHz = 30.0;
    using Transport = std::function<bool(const juce::OSCBundle&)>;

    OscSender();
    ~OscSender();

    /// Connects a UDP sender to host:port. False if it cannot be set up.
    bool connect(const std::string& host, int port);
    void disconnect();
    /// Replaces the UDP transport (tests).
    void setTransport(Transport transport) { transport_ = std::move(transport); }

    /// Fold one engine feedback item in. Positions: channel 0 of each source.
    /// Speaker meters are linear peaks (post limiter).
    void onFeedback(const Feedback& feedback);
    void setCurrentCue(int index);

    /// Sends a bundle if >= 1/30 s passed since the last one and anything
    /// changed. Returns true if one was sent.
    bool tick(double nowSeconds);

    /// What the next bundle would contain (clears the changed set).
    juce::OSCBundle takePending();
    bool hasPending() const noexcept {
        return !pendingPos_.empty() || !pendingMeter_.empty() || beatDirty_ || cueDirty_;
    }

private:
    Transport transport_;
    juce::OSCSender sender_;
    bool connected_{false};
    double lastSent_{-1.0e9};

    std::map<int, Position> pendingPos_;
    std::map<int, float> pendingMeter_;
    double beat_{0.0};
    bool beatDirty_{false};
    int cue_{-1};
    bool cueDirty_{false};
};

}  // namespace spacer
