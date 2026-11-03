#pragma once

#include <atomic>
#include <mutex>
#include <optional>
#include <thread>

#include "engine/PluginHost.h"
#include "engine/PluginScanProgress.h"

namespace spacer {

/// A full plugin scan on a worker thread (SPEC §13: full scans run off the
/// message thread; each plugin is still scanned in its own process by the
/// host's PluginScanWorker). Progress can be read from any thread, the scan
/// can be cancelled, and the message thread adopts the result with poll().
///
/// The thread works on a private copy of the host's PluginScanCache, so the
/// message thread keeps reading the old cache (insert menus, resolving
/// slots) without locks; poll() swaps the finished copy in. While the job
/// runs the host does not scan unknown plugins on demand.
///
/// start(), cancel() and poll() are for the message thread; progress() is
/// thread-safe.
class PluginScanJob {
public:
    /// `worker` (optional) replaces the host's scan worker; tests use it.
    explicit PluginScanJob(PluginHost& host, PluginScanWorker* worker = nullptr);
    /// Cancels and waits for the thread (at most the plugin being scanned).
    ~PluginScanJob();
    PluginScanJob(const PluginScanJob&) = delete;
    PluginScanJob& operator=(const PluginScanJob&) = delete;

    /// Starts a scan; false if one is already running. `rescanAll` scans every
    /// plugin again, also cached and blacklisted ones.
    bool start(bool rescanAll = false);
    /// Asks the scan to stop before the next plugin. Keeps what was scanned.
    void cancel();
    bool running() const;
    PluginScanProgress progress() const;

    /// Message thread, call regularly (e.g. from a timer): once the thread has
    /// ended, joins it, adopts its cache into the host and returns true (once
    /// per scan). The result is then in progress() (Finished or Cancelled).
    bool poll();

private:
    void run(bool rescanAll);
    void update(const std::function<void(PluginScanProgress&)>& change);

    PluginHost& host_;
    PluginScanWorker& worker_;
    std::thread thread_;
    std::optional<PluginScanCache> staging_;  // owned by the thread while it runs
    std::atomic<bool> cancel_{false};
    std::atomic<bool> threadDone_{false};
    mutable std::mutex mutex_;
    PluginScanProgress progress_;
};

}  // namespace spacer
