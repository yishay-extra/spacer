#include "control/OscServer.h"

#include <algorithm>
#include <cstdint>

#include "control/OscRouter.h"

namespace spacer {

namespace {
constexpr int kMaxBundleDepth = 8;

double toEpochSeconds(const juce::OSCTimeTag& tag) {
    if (tag.isImmediately()) return 0.0;
    // NTP 32.32 fixed point since 1900, kept at full (sub-sample) resolution.
    constexpr double kNtpToUnix = 2208988800.0;
    const std::uint64_t raw = tag.getRawTimeTag();
    return static_cast<double>(raw >> 32) - kNtpToUnix +
           static_cast<double>(raw & 0xffffffffu) / 4294967296.0;
}
}  // namespace

OscServer::OscServer() : receiver_("Spacer OSC in") {
    receiver_.addListener(this);
    receiver_.registerFormatErrorHandler([this](const char*, int) { handleMalformedPacket(); });
}

OscServer::~OscServer() {
    stop();
    receiver_.removeListener(this);
}

bool OscServer::start(int port) {
    stop();
    running_ = receiver_.connect(port);
    return running_;
}

void OscServer::stop() {
    if (running_) receiver_.disconnect();
    running_ = false;
}

void OscServer::handleMessage(const juce::OSCMessage& message, double dueEpochSeconds) {
    received_.fetch_add(1, std::memory_order_relaxed);
    auto routed = OscRouter::route(message);
    if (routed.malformed || !routed.event) {
        malformedMessages_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (std::holds_alternative<ev::ClockBeat>(*routed.event) ||
        std::holds_alternative<ev::ClockStop>(*routed.event)) {
        const std::lock_guard<std::mutex> lock(clockTapMutex_);
        if (clockTap_) {
            double hostTime = juce::Time::getMillisecondCounterHiRes() * 0.001;
            if (dueEpochSeconds > 0.0)
                hostTime += dueEpochSeconds - static_cast<double>(juce::Time::currentTimeMillis()) * 0.001;
            if (clockTap_(*routed.event, hostTime)) return;
        }
    }
    inbox_.push(Timed{dueEpochSeconds, std::move(*routed.event)});
}

void OscServer::handleBundle(const juce::OSCBundle& bundle) {
    handleBundle(bundle, 0);
}

void OscServer::handleBundle(const juce::OSCBundle& bundle, int depth) {
    if (depth > kMaxBundleDepth) {
        malformedPackets_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    const double due = toEpochSeconds(bundle.getTimeTag());
    for (const auto& element : bundle) {
        if (element.isMessage())
            handleMessage(element.getMessage(), due);
        else if (element.isBundle())
            handleBundle(element.getBundle(), depth + 1);
    }
}

void OscServer::oscMessageReceived(const juce::OSCMessage& message) {
    handleMessage(message);
}

void OscServer::oscBundleReceived(const juce::OSCBundle& bundle) {
    handleBundle(bundle);
}

namespace {
/// Events that become engine commands (sample-accurate when timed); the rest
/// (cues, scenes, triggers) act on the message thread when due.
bool isEngineEvent(const ControlEvent& e) {
    return !(std::holds_alternative<ev::CueGo>(e) || std::holds_alternative<ev::CueBack>(e) ||
             std::holds_alternative<ev::CueJump>(e) || std::holds_alternative<ev::RecallScene>(e) ||
             std::holds_alternative<ev::RecallSceneIndex>(e) || std::holds_alternative<ev::OscTrigger>(e) ||
             std::holds_alternative<ev::MidiTrigger>(e));
}
}  // namespace

void OscServer::pump(double nowEpochSeconds, const std::function<void(const ControlEvent&)>& deliver) {
    pump(nowEpochSeconds, 0.0, [&deliver](const ControlEvent& e, double) { deliver(e); });
}

void OscServer::pump(double nowEpochSeconds, double lookaheadSeconds,
                     const std::function<void(const ControlEvent&, double dueEpochSeconds)>& deliver) {
    scratch_.clear();
    inbox_.drainTo(scratch_);
    for (auto& t : scratch_) {
        if (scheduled_.size() >= kMaxScheduled) {
            scheduleDropped_.fetch_add(1, std::memory_order_relaxed);
            continue;
        }
        scheduled_.emplace(t.due, std::move(t.event));  // multimap keeps arrival order within equal keys
    }
    scratch_.clear();
    // Due events, plus engine events due within the look-ahead: those are
    // handed over early with their timetag so the engine can apply them at
    // the exact sample.
    const double horizon = nowEpochSeconds + std::max(0.0, lookaheadSeconds);
    for (auto it = scheduled_.begin(); it != scheduled_.end() && it->first <= horizon;) {
        const double due = it->first;
        if (due > nowEpochSeconds && !isEngineEvent(it->second)) {
            ++it;
            continue;
        }
        auto node = scheduled_.extract(it++);
        deliver(node.mapped(), due);
    }
}

void OscServer::setClockTap(ClockTap tap) {
    const std::lock_guard<std::mutex> lock(clockTapMutex_);
    clockTap_ = std::move(tap);
}

OscServer::Stats OscServer::stats() const noexcept {
    return {received_.load(), malformedPackets_.load(), malformedMessages_.load(),
            inbox_.dropped() + scheduleDropped_.load()};
}

}  // namespace spacer
