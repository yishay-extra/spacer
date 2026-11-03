#pragma once

// OSC address router (SPEC §12.1): one juce::OSCMessage → one ControlEvent.
// Pure function of the message, safe to call from any thread.

#include <juce_osc/juce_osc.h>

#include <optional>

#include "control/ControlEvent.h"

namespace spacer {

class OscRouter {
public:
    struct Result {
        /// Set unless malformed. An address outside the SPEC §12.1 space comes
        /// back as ev::OscTrigger (the dispatcher decides whether a cue wants it).
        std::optional<ControlEvent> event;
        /// A known address with wrong argument count / types / non-finite numbers.
        bool malformed{false};
    };

    static Result route(const juce::OSCMessage& message);

private:
    static Result routeAddress(const juce::OSCMessage& message);
};

}  // namespace spacer
