#include "control/OscServer.h"

#include "control/OscRouter.h"

namespace spacer {

namespace {
constexpr int kMaxBundleDepth = 8;

double toEpochSeconds(const juce::OSCTimeTag& tag) {
    if (tag.isImmediately()) return 0.0;
    return static_cast<double>(tag.toTime().toMilliseconds()) / 1000.0;
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

void OscServer::pump(double nowEpochSeconds, const std::function<void(const ControlEvent&)>& deliver) {
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
    while (!scheduled_.empty() && scheduled_.begin()->first <= nowEpochSeconds) {
        auto node = scheduled_.extract(scheduled_.begin());
        deliver(node.mapped());
    }
}

OscServer::Stats OscServer::stats() const noexcept {
    return {received_.load(), malformedPackets_.load(), malformedMessages_.load(),
            inbox_.dropped() + scheduleDropped_.load()};
}

}  // namespace spacer
