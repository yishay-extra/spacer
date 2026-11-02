#include "control/OscSender.h"

#include <cmath>

namespace spacer {

OscSender::OscSender() = default;
OscSender::~OscSender() {
    disconnect();
}

bool OscSender::connect(const std::string& host, int port) {
    disconnect();
    connected_ = sender_.connect(host, port);
    if (connected_) transport_ = [this](const juce::OSCBundle& b) { return sender_.send(b); };
    return connected_;
}

void OscSender::disconnect() {
    if (connected_) sender_.disconnect();
    connected_ = false;
}

void OscSender::onFeedback(const Feedback& feedback) {
    if (const auto* p = std::get_if<fb::SourcePosition>(&feedback)) {
        if (p->channel == 0) pendingPos_[p->sourceId] = p->position;
    } else if (const auto* m = std::get_if<fb::SpeakerMeter>(&feedback)) {
        pendingMeter_[m->speakerId] = m->peak;
    } else if (const auto* c = std::get_if<fb::Clock>(&feedback)) {
        // Only while the transport runs, and only when the beat moved.
        if (c->state.playing && std::fabs(c->state.beat - beat_) > 1e-9) {
            beat_ = c->state.beat;
            beatDirty_ = true;
        }
    }
}

void OscSender::setCurrentCue(int index) {
    if (index == cue_) return;
    cue_ = index;
    cueDirty_ = true;
}

juce::OSCBundle OscSender::takePending() {
    juce::OSCBundle bundle;
    for (const auto& [id, pos] : pendingPos_)
        bundle.addElement(
            juce::OSCMessage(juce::OSCAddressPattern("/spacer/source/" + juce::String(id) + "/pos/aed"),
                             pos.azimuth, pos.elevation, pos.distance));
    for (const auto& [id, peak] : pendingMeter_)
        bundle.addElement(
            juce::OSCMessage(juce::OSCAddressPattern("/spacer/meter/speaker/" + juce::String(id)), peak));
    if (beatDirty_)
        bundle.addElement(
            juce::OSCMessage(juce::OSCAddressPattern("/spacer/clock/beat"), static_cast<float>(beat_)));
    if (cueDirty_) bundle.addElement(juce::OSCMessage(juce::OSCAddressPattern("/spacer/cue/current"), cue_));
    pendingPos_.clear();
    pendingMeter_.clear();
    beatDirty_ = cueDirty_ = false;
    return bundle;
}

bool OscSender::tick(double nowSeconds) {
    if (!transport_ || !hasPending() || nowSeconds - lastSent_ < 1.0 / kRateHz) return false;
    lastSent_ = nowSeconds;
    return transport_(takePending());
}

}  // namespace spacer
