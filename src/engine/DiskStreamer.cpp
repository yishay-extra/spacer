#include "engine/DiskStreamer.h"

#include <algorithm>

namespace spacer {

DiskStreamer::DiskStreamer() : juce::Thread("Spacer disk") {}

DiskStreamer::~DiskStreamer() {
    stop();
}

void DiskStreamer::start() {
    if (!isThreadRunning()) startThread(juce::Thread::Priority::high);
}

void DiskStreamer::stop() {
    if (!isThreadRunning()) return;
    signalThreadShouldExit();
    notify();
    waitForThreadToExit(-1);
}

void DiskStreamer::add(const std::shared_ptr<FileStream>& stream) {
    if (!stream) return;
    const std::lock_guard lock(listLock_);
    streams_.push_back(stream);
}

int DiskStreamer::numStreams() {
    const std::lock_guard lock(listLock_);
    return static_cast<int>(
        std::count_if(streams_.begin(), streams_.end(), [](const auto& w) { return !w.expired(); }));
}

void DiskStreamer::serviceAll() {
    const std::lock_guard service(serviceLock_);
    {
        const std::lock_guard lock(listLock_);
        std::erase_if(streams_, [](const auto& w) { return w.expired(); });
        snapshot_.clear();
        for (const auto& w : streams_)
            if (auto s = w.lock()) snapshot_.push_back(std::move(s));
    }
    // Read without holding listLock_, so a stalled reader cannot block add().
    for (const auto& s : snapshot_) {
        if (threadShouldExit()) break;
        s->service();
    }
    // Streams whose player is gone are destroyed here, on this thread.
    snapshot_.clear();
}

void DiskStreamer::run() {
    while (!threadShouldExit()) {
        serviceAll();
        wait(kPollMs);
    }
}

}  // namespace spacer
