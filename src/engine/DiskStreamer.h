#pragma once

#include <juce_core/juce_core.h>

#include <memory>
#include <mutex>
#include <vector>

#include "engine/FileStream.h"

namespace spacer {

/// The disk thread (SPEC §5.1): keeps every registered FileStream's ring
/// filled. Streams are held weakly, so a stream lives exactly as long as the
/// FilePlayer(s) owning it plus any service pass in progress.
///
/// The audio thread never waits for this thread: it only reads the rings and
/// atomics of the streams. A reader that stalls (slow disk, network volume)
/// therefore only causes underruns (silence), and stop() blocks only its
/// caller (message thread) until the stalled read returns.
///
/// Without start() nothing runs in the background; serviceAll() can then be
/// called directly (the offline renderer does so before every block, which
/// makes offline renders independent of disk timing).
class DiskStreamer : private juce::Thread {
public:
    static constexpr int kPollMs = 2;

    DiskStreamer();
    ~DiskStreamer() override;

    /// Message thread. Starts the background thread (no-op if running).
    void start();
    /// Message thread. Asks the thread to exit and waits for it.
    void stop();
    bool running() const { return isThreadRunning(); }

    /// Any non-audio thread.
    void add(const std::shared_ptr<FileStream>& stream);
    /// Services every live stream once. Any non-audio thread.
    void serviceAll();
    int numStreams();

private:
    void run() override;

    std::mutex listLock_;     // guards streams_ (message ↔ disk thread only)
    std::mutex serviceLock_;  // one service pass at a time
    std::vector<std::weak_ptr<FileStream>> streams_;
    std::vector<std::shared_ptr<FileStream>> snapshot_;  // under serviceLock_
};

}  // namespace spacer
