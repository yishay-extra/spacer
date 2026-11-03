// Background plugin scan (WP11e, SPEC §13): progress, cancel, adoption of the
// result, and the cache staying consistent for the message thread meanwhile.

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "TestPlugins.h"
#include "engine/PluginHost.h"
#include "engine/PluginScanJob.h"

using namespace spacer;
using namespace spacer::testing;
using State = PluginScanProgress::State;

namespace {

struct TempDir {
    juce::File dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                         .getNonexistentChildFile("spacer-scanjob-test", "", false);
    TempDir() { dir.createDirectory(); }
    ~TempDir() { dir.deleteRecursively(); }
};

/// Keeps TestPluginFormat::candidates for the duration of a test.
struct Candidates {
    std::vector<juce::String> saved = TestPluginFormat::candidates;
    explicit Candidates(std::vector<juce::String> ids) { TestPluginFormat::candidates = std::move(ids); }
    ~Candidates() { TestPluginFormat::candidates = saved; }
};

/// Scan worker that waits for the test to let each plugin through, so the
/// test can look at the job while it is mid-scan.
struct GatedWorker : PluginScanWorker {
    std::mutex mutex;
    std::condition_variable cv;
    int entered{0};
    int allowed{0};
    std::vector<juce::String> ids;

    PluginScanResult scan(const juce::String& format, const juce::String& id) override {
        std::unique_lock<std::mutex> lock(mutex);
        ids.push_back(id);
        ++entered;
        cv.notify_all();
        cv.wait(lock, [this] { return allowed >= entered; });
        PluginScanResult r;
        r.status = PluginScanResult::Status::Ok;
        juce::PluginDescription d;
        d.name = id;
        d.pluginFormatName = format;
        d.fileOrIdentifier = id;
        r.found.push_back(d);
        return r;
    }
    bool waitEntered(int n) {
        std::unique_lock<std::mutex> lock(mutex);
        return cv.wait_for(lock, std::chrono::seconds(10), [&] { return entered >= n; });
    }
    void allowAll() {
        const std::lock_guard<std::mutex> lock(mutex);
        allowed = 1 << 20;
        cv.notify_all();
    }
    void allowOne() {
        const std::lock_guard<std::mutex> lock(mutex);
        ++allowed;
        cv.notify_all();
    }
};

bool waitFor(PluginScanJob& job, int timeoutMs = 20000) {
    for (int waited = 0; waited < timeoutMs; waited += 5) {
        if (job.poll()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

PluginHost::Options inProcess(const juce::File& dir) {
    PluginHost::Options o;
    o.cacheDirectory = dir;
    return o;
}

}  // namespace

TEST_CASE("PluginScanJob: scans off the calling thread, reports progress, adopts the result") {
    TempDir tmp;
    Candidates candidates(
        {TestPluginFormat::id("gain"), TestPluginFormat::id("delay8"), TestPluginFormat::id("swap")});
    PluginHost host(inProcess(tmp.dir), addTestFormat);
    GatedWorker worker;
    PluginScanJob job(host, &worker);
    CHECK(job.progress().state == State::Idle);

    REQUIRE(job.start());
    CHECK_FALSE(job.start());  // already running
    REQUIRE(worker.waitEntered(1));
    // The call returned while the first plug-in is still being scanned.
    auto p = job.progress();
    CHECK(p.state == State::Running);
    CHECK(p.active());
    CHECK(p.total == 3);
    CHECK(p.index == 0);
    CHECK(p.current == TestPluginFormat::id("gain").toStdString());
    // The host keeps its old cache until the result is adopted, and does not
    // scan unknown plug-ins on demand meanwhile.
    CHECK(host.cache().known().empty());
    CHECK(host.backgroundScanActive());
    CHECK_FALSE(host.resolve(TestPluginFormat::id("gain").toStdString()));
    CHECK_FALSE(job.poll());

    worker.allowOne();
    REQUIRE(worker.waitEntered(2));
    p = job.progress();
    CHECK(p.index == 1);
    CHECK(p.current == TestPluginFormat::id("delay8").toStdString());
    CHECK(p.fraction() > 0.3);
    CHECK(p.fraction() < 0.4);

    worker.allowAll();
    REQUIRE(waitFor(job));
    p = job.progress();
    CHECK(p.state == State::Finished);
    CHECK(p.done());
    CHECK(p.scanned == 3);
    CHECK(p.added == 3);
    CHECK(p.fraction() == 1.0);
    CHECK_FALSE(host.backgroundScanActive());
    CHECK(host.cache().known().size() == 3);
    CHECK(host.cache().hasIdentifier(TestPluginFormat::id("swap")));
    CHECK_FALSE(job.poll());  // reported once
    CHECK(job.start());       // and a new scan can start
    worker.allowAll();
    REQUIRE(waitFor(job));
    CHECK(job.progress().skipped == 3);  // everything is cached now
}

TEST_CASE("PluginScanJob: cancel stops before the next plug-in and keeps what was scanned") {
    TempDir tmp;
    Candidates candidates({TestPluginFormat::id("gain"), TestPluginFormat::id("delay8"),
                           TestPluginFormat::id("swap"), TestPluginFormat::id("delay64")});
    PluginHost host(inProcess(tmp.dir), addTestFormat);
    GatedWorker worker;
    PluginScanJob job(host, &worker);

    REQUIRE(job.start());
    REQUIRE(worker.waitEntered(1));
    worker.allowOne();
    REQUIRE(worker.waitEntered(2));
    job.cancel();
    CHECK(job.progress().state == State::Cancelling);
    CHECK(job.running());
    worker.allowAll();  // the plug-in being scanned finishes, nothing after it starts
    REQUIRE(waitFor(job));

    const auto p = job.progress();
    CHECK(p.state == State::Cancelled);
    CHECK(p.scanned == 2);
    CHECK(worker.ids.size() == 2);
    CHECK(host.cache().known().size() == 2);
    // The partial result was saved, so a restart sees it.
    PluginScanCache reloaded(tmp.dir);
    reloaded.load();
    CHECK(reloaded.known().size() == 2);

    // A later scan picks up where this one stopped.
    REQUIRE(job.start());
    REQUIRE(waitFor(job));
    CHECK(job.progress().state == State::Finished);
    CHECK(job.progress().scanned == 2);
    CHECK(job.progress().skipped == 2);
    CHECK(host.cache().known().size() == 4);
}

TEST_CASE("PluginScanJob: destroying a running job cancels it and joins the thread") {
    TempDir tmp;
    Candidates candidates({TestPluginFormat::id("gain"), TestPluginFormat::id("delay8")});
    PluginHost host(inProcess(tmp.dir), addTestFormat);
    GatedWorker worker;
    {
        PluginScanJob job(host, &worker);
        REQUIRE(job.start());
        REQUIRE(worker.waitEntered(1));
        std::thread release([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            worker.allowAll();
        });
        // ~PluginScanJob: cancel, wait for the plug-in in flight.
        job.cancel();
        release.detach();
    }
    CHECK_FALSE(host.backgroundScanActive());
    CHECK(worker.ids.size() == 1);  // the second plug-in never started
}

TEST_CASE("PluginScanJob: a rescan visits cached and blacklisted plug-ins again") {
    TempDir tmp;
    Candidates candidates({TestPluginFormat::id("gain"), TestPluginFormat::id("delay8")});
    PluginHost host(inProcess(tmp.dir), addTestFormat);
    host.cache().addToBlacklist(TestPluginFormat::id("delay8"), "failed: earlier");
    GatedWorker worker;
    worker.allowAll();
    PluginScanJob job(host, &worker);

    REQUIRE(job.start(false));
    REQUIRE(waitFor(job));
    CHECK(job.progress().scanned == 1);  // the blacklisted one is skipped
    CHECK(host.cache().isBlacklisted(TestPluginFormat::id("delay8")));

    REQUIRE(job.start(true));
    REQUIRE(waitFor(job));
    CHECK(job.progress().scanned == 2);
    CHECK_FALSE(host.cache().isBlacklisted(TestPluginFormat::id("delay8")));  // it scanned fine this time
    CHECK(host.cache().hasIdentifier(TestPluginFormat::id("delay8")));
}

TEST_CASE("PluginScanJob: each plug-in still scans in its own process; a crash is blacklisted and shown") {
    TempDir tmp;
    Candidates candidates(
        {TestPluginFormat::id("gain"), TestPluginFormat::id("crash"), TestPluginFormat::id("delay8")});
    PluginHost::Options options;
    options.cacheDirectory = tmp.dir;
    options.scannerExecutable = juce::File(SPACER_TEST_PLUGIN_SCANNER);
    options.scanTimeoutMs = 2000;
    PluginHost host(options, addTestFormat);
    REQUIRE(host.scansOutOfProcess());

    PluginScanJob job(host);  // the host's own worker: the child-process scanner
    REQUIRE(job.start());
    REQUIRE(waitFor(job));
    const auto p = job.progress();
    CHECK(p.state == State::Finished);
    CHECK(p.scanned == 3);
    CHECK(p.added == 2);
    REQUIRE(p.blacklisted.size() == 1);
    CHECK(p.blacklisted[0] == TestPluginFormat::id("crash").toStdString());

    // Blacklist visibility: the host's cache lists it with the reason.
    REQUIRE(host.cache().blacklist().size() == 1);
    CHECK(host.cache().blacklist()[0].identifier == TestPluginFormat::id("crash"));
    CHECK(host.cache().blacklist()[0].reason.startsWith("crashed"));
    CHECK(host.cache().hasIdentifier(TestPluginFormat::id("gain")));
    CHECK(host.cache().hasIdentifier(TestPluginFormat::id("delay8")));

    // "Retry" = take it off the blacklist and scan again.
    host.cache().removeFromBlacklist(TestPluginFormat::id("crash"));
    REQUIRE(job.start());
    REQUIRE(waitFor(job));
    CHECK(job.progress().scanned == 1);
    CHECK(host.cache().isBlacklisted(TestPluginFormat::id("crash")));  // it crashes again
}

TEST_CASE("PluginScanner: ShouldCancel stops between plug-ins and reports it") {
    TempDir tmp;
    Candidates candidates(
        {TestPluginFormat::id("gain"), TestPluginFormat::id("delay8"), TestPluginFormat::id("swap")});
    PluginHost host(inProcess(tmp.dir), addTestFormat);
    int seen = 0;
    const auto report = host.scanAll(
        false, [&seen](int, int, const juce::String&) { ++seen; }, [&seen] { return seen >= 1; });
    CHECK(report.cancelled);
    CHECK(report.scanned == 1);
    CHECK(host.cache().known().size() == 1);
    // Progress runs over the whole scan, not per format.
    int total = 0;
    PluginHost again(inProcess(tmp.dir), addTestFormat);
    again.scanAll(true, [&total](int, int t, const juce::String&) { total = t; });
    CHECK(total == 3);
}
