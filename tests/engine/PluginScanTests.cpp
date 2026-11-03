#include <catch2/catch_test_macros.hpp>
#include <map>

#include "TestPlugins.h"
#include "engine/PluginHost.h"
#include "engine/PluginScanCache.h"
#include "engine/PluginScanProcess.h"
#include "engine/PluginScanner.h"

using namespace spacer;
using namespace spacer::testing;

namespace {

struct TempDir {
    juce::File dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                         .getNonexistentChildFile("spacer-scan-test", "", false);
    TempDir() { dir.createDirectory(); }
    ~TempDir() { dir.deleteRecursively(); }
};

/// Scripted worker: answers from a table, counts calls, checks the pedal.
struct FakeWorker : PluginScanWorker {
    PluginScanCache* cache{nullptr};
    std::map<juce::String, PluginScanResult::Status> outcomes;
    std::vector<juce::String> calls;
    bool pedalSeen{true};
    PluginScanResult scan(const juce::String& format, const juce::String& id) override {
        calls.push_back(id);
        if (cache) pedalSeen = pedalSeen && cache->pedalFile().loadFileAsString() == id;
        PluginScanResult r;
        const auto it = outcomes.find(id);
        r.status = it == outcomes.end() ? PluginScanResult::Status::Ok : it->second;
        if (r.status == PluginScanResult::Status::Ok) {
            juce::PluginDescription d;
            d.name = id;
            d.pluginFormatName = format;
            d.fileOrIdentifier = id;
            r.found.push_back(d);
        }
        return r;
    }
};

juce::File testScanner() {
    return juce::File(SPACER_TEST_PLUGIN_SCANNER);
}

}  // namespace

TEST_CASE("PluginScanCache: known plugins and the blacklist survive save / load") {
    TempDir tmp;
    {
        PluginScanCache cache(tmp.dir);
        CHECK(cache.load());  // no file yet: empty, fine
        juce::PluginDescription d;
        d.name = "Delay";
        d.pluginFormatName = "AudioUnit";
        d.fileOrIdentifier = "AudioUnit:Effects/aufx,dely,appl";
        d.uniqueId = 42;
        cache.add(d);
        cache.add(d);  // replaces, does not duplicate
        cache.addToBlacklist("AudioUnit:Effects/aufx,bad!,xxxx", "crashed");
        CHECK(cache.save());
    }
    PluginScanCache cache(tmp.dir);
    CHECK(cache.load());
    REQUIRE(cache.known().size() == 1);
    CHECK(cache.find("AudioUnit:Effects/aufx,dely,appl"));
    CHECK(cache.find(cache.known()[0].createIdentifierString()));
    CHECK_FALSE(cache.find("AudioUnit:Effects/aufx,none,appl"));
    CHECK(cache.isBlacklisted("AudioUnit:Effects/aufx,bad!,xxxx"));
    CHECK_FALSE(cache.recoveredCrash());

    cache.removeFromBlacklist("AudioUnit:Effects/aufx,bad!,xxxx");
    CHECK(cache.blacklist().empty());
}

TEST_CASE("PluginScanCache: an unreadable cache file starts empty") {
    TempDir tmp;
    tmp.dir.getChildFile("plugin-cache.xml").replaceWithText("<<< not xml");
    PluginScanCache cache(tmp.dir);
    CHECK_FALSE(cache.load());
    CHECK(cache.known().empty());
}

TEST_CASE("PluginScanCache: a pedal left by a crashed scan blacklists that plugin") {
    TempDir tmp;
    {
        PluginScanCache cache(tmp.dir);
        cache.beginScan("SpacerTest:evil");
        // ... the process dies here: endScan() never runs.
    }
    PluginScanCache cache(tmp.dir);
    cache.load();
    REQUIRE(cache.recoveredCrash());
    CHECK(*cache.recoveredCrash() == "SpacerTest:evil");
    CHECK(cache.isBlacklisted("SpacerTest:evil"));
    CHECK_FALSE(cache.pedalFile().exists());

    // The blacklisting was saved, and a host reports it once.
    PluginScanCache reloaded(tmp.dir);
    reloaded.load();
    CHECK(reloaded.isBlacklisted("SpacerTest:evil"));
}

TEST_CASE("PluginScanner: failures are blacklisted and skipped until a full rescan") {
    TempDir tmp;
    juce::AudioPluginFormatManager formats;
    addTestFormat(formats);
    PluginScanCache cache(tmp.dir);
    FakeWorker worker;
    worker.cache = &cache;
    worker.outcomes = {{"SpacerTest:a", PluginScanResult::Status::Crashed},
                       {"SpacerTest:b", PluginScanResult::Status::TimedOut},
                       {"SpacerTest:c", PluginScanResult::Status::Failed}};
    PluginScanner scanner(formats, cache, worker);
    const std::vector<juce::String> ids{"SpacerTest:a", "SpacerTest:b", "SpacerTest:c", "SpacerTest:d"};

    auto report = scanner.scanIdentifiers(TestPluginFormat::kName, ids);
    CHECK(report.scanned == 4);
    CHECK(report.added == 1);
    CHECK(report.blacklisted.size() == 3);
    CHECK(worker.pedalSeen);
    CHECK_FALSE(cache.pedalFile().exists());
    CHECK(cache.isBlacklisted("SpacerTest:a"));
    CHECK(cache.blacklist()[0].reason.startsWith("crashed"));
    CHECK(cache.blacklist()[1].reason.startsWith("timed out"));
    CHECK(cache.hasIdentifier("SpacerTest:d"));

    // Second pass: everything is either cached or blacklisted.
    worker.calls.clear();
    report = scanner.scanIdentifiers(TestPluginFormat::kName, ids);
    CHECK(worker.calls.empty());
    CHECK(report.skipped == 4);

    // A full rescan retries; "a" now works and leaves the blacklist.
    worker.outcomes.erase("SpacerTest:a");
    report = scanner.scanIdentifiers(TestPluginFormat::kName, ids, true);
    CHECK(worker.calls.size() == 4);
    CHECK_FALSE(cache.isBlacklisted("SpacerTest:a"));
    CHECK(cache.hasIdentifier("SpacerTest:a"));

    // Results were saved.
    PluginScanCache reloaded(tmp.dir);
    reloaded.load();
    CHECK(reloaded.hasIdentifier("SpacerTest:a"));
    CHECK(reloaded.isBlacklisted("SpacerTest:b"));
}

TEST_CASE("PluginScanner: scanAll covers every format's candidates") {
    TempDir tmp;
    juce::AudioPluginFormatManager formats;
    addTestFormat(formats);
    PluginScanCache cache(tmp.dir);
    InProcessScanWorker worker(formats);
    PluginScanner scanner(formats, cache, worker);
    CHECK(scanner.candidates(*formats.getFormat(0)).size() == TestPluginFormat::candidates.size());
    const auto report = scanner.scanAll();
    CHECK(report.added == static_cast<int>(TestPluginFormat::candidates.size()));
    CHECK(cache.find(TestPluginFormat::id("delay64").toStdString()));
    CHECK(scanner.scanAll().skipped == static_cast<int>(TestPluginFormat::candidates.size()));
}

TEST_CASE("ChildProcessScanWorker: scans out of process and survives crashes and hangs") {
    REQUIRE(testScanner().existsAsFile());
    ChildProcessScanWorker worker(testScanner(), 2000);

    const auto ok = worker.scan(TestPluginFormat::kName, TestPluginFormat::id("delay64"));
    CHECK(ok.status == PluginScanResult::Status::Ok);
    REQUIRE(ok.found.size() == 1);
    CHECK(ok.found[0].fileOrIdentifier == TestPluginFormat::id("delay64"));

    const auto crash = worker.scan(TestPluginFormat::kName, TestPluginFormat::id("crash"));
    CHECK(crash.status == PluginScanResult::Status::Crashed);

    const auto hang = worker.scan(TestPluginFormat::kName, TestPluginFormat::id("hang"));
    CHECK(hang.status == PluginScanResult::Status::TimedOut);

    const auto unknownFormat = worker.scan("NoSuchFormat", "x");
    CHECK(unknownFormat.status == PluginScanResult::Status::Failed);

    ChildProcessScanWorker missing(juce::File("/nonexistent/scanner"));
    CHECK(missing.scan(TestPluginFormat::kName, TestPluginFormat::id("gain")).status ==
          PluginScanResult::Status::Failed);
}

TEST_CASE("PluginHost: out-of-process scanning blacklists a crashing plugin and keeps going") {
    TempDir tmp;
    PluginHost::Options options;
    options.cacheDirectory = tmp.dir;
    options.scannerExecutable = testScanner();
    options.scanTimeoutMs = 2000;
    const auto saved = TestPluginFormat::candidates;
    TestPluginFormat::candidates = {TestPluginFormat::id("gain"), TestPluginFormat::id("crash"),
                                    TestPluginFormat::id("delay8")};
    {
        PluginHost host(options, addTestFormat);
        CHECK(host.scansOutOfProcess());
        const auto report = host.scanAll();
        CHECK(report.added == 2);
        REQUIRE(report.blacklisted.size() == 1);
        CHECK(report.blacklisted[0] == TestPluginFormat::id("crash"));
        CHECK(host.resolve(TestPluginFormat::id("delay8").toStdString()));
        // On demand: a blacklisted plugin is not retried.
        CHECK_FALSE(host.resolve(TestPluginFormat::id("crash").toStdString()));
    }
    TestPluginFormat::candidates = saved;
}
