#include "engine/PluginScanJob.h"

namespace spacer {

PluginScanJob::PluginScanJob(PluginHost& host, PluginScanWorker* worker)
    : host_(host), worker_(worker != nullptr ? *worker : host.scanWorker()) {}

PluginScanJob::~PluginScanJob() {
    cancel();
    if (thread_.joinable()) thread_.join();
    host_.setBackgroundScanActive(false);
}

bool PluginScanJob::start(bool rescanAll) {
    if (thread_.joinable() || running()) return false;
    staging_ = host_.cache();
    cancel_.store(false);
    threadDone_.store(false);
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        progress_ = {};
        progress_.state = PluginScanProgress::State::Running;
    }
    host_.setBackgroundScanActive(true);
    thread_ = std::thread([this, rescanAll] { run(rescanAll); });
    return true;
}

void PluginScanJob::cancel() {
    cancel_.store(true);
    update([](PluginScanProgress& p) {
        if (p.state == PluginScanProgress::State::Running) p.state = PluginScanProgress::State::Cancelling;
    });
}

bool PluginScanJob::running() const {
    return progress().active();
}

PluginScanProgress PluginScanJob::progress() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return progress_;
}

void PluginScanJob::update(const std::function<void(PluginScanProgress&)>& change) {
    const std::lock_guard<std::mutex> lock(mutex_);
    change(progress_);
}

void PluginScanJob::run(bool rescanAll) {
    PluginScanner scanner(host_.formats(), *staging_, worker_);
    PluginScanner::Report report;
    try {
        report = scanner.scanAll(
            rescanAll,
            [this](int index, int total, const juce::String& id) {
                update([&](PluginScanProgress& p) {
                    p.index = index;
                    p.total = total;
                    p.current = id.toStdString();
                });
            },
            [this] { return cancel_.load(); });
    } catch (...) {
        // A scan that throws counts as stopped: keep what the cache holds.
        report.cancelled = true;
    }
    update([&](PluginScanProgress& p) {
        p.scanned = report.scanned;
        p.skipped = report.skipped;
        p.added = report.added;
        p.blacklisted.clear();
        for (const auto& id : report.blacklisted) p.blacklisted.push_back(id.toStdString());
        p.current.clear();
        p.state =
            report.cancelled ? PluginScanProgress::State::Cancelled : PluginScanProgress::State::Finished;
    });
    threadDone_.store(true);
}

bool PluginScanJob::poll() {
    if (!thread_.joinable() || !threadDone_.load()) return false;
    thread_.join();
    host_.adoptScanCache(std::move(*staging_));
    staging_.reset();
    host_.setBackgroundScanActive(false);
    return true;
}

}  // namespace spacer
