#pragma once

#include <juce_audio_processors_headless/juce_audio_processors_headless.h>

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "engine/PluginScanCache.h"

namespace spacer {

/// Outcome of scanning one plugin identifier.
struct PluginScanResult {
    enum class Status { Ok, Failed, Crashed, TimedOut };
    Status status{Status::Failed};
    std::vector<juce::PluginDescription> found;  // Ok only (may be empty: nothing in that file)
    std::string message;
};

/// Scans one identifier of one format. The production worker runs the scan
/// in a child process (SPEC §5.1: scanning is out of process) so a crashing
/// or hanging plugin cannot take the app down.
class PluginScanWorker {
public:
    virtual ~PluginScanWorker() = default;
    virtual PluginScanResult scan(const juce::String& formatName, const juce::String& identifier) = 0;
};

/// Runs `executable --scan <format> <identifier> <resultFile>` (see
/// PluginScanProcess.h) and reads the result file. No result file = crashed;
/// still running after `timeoutMs` = killed, timed out.
class ChildProcessScanWorker : public PluginScanWorker {
public:
    ChildProcessScanWorker(juce::File executable, int timeoutMs = 30000);
    PluginScanResult scan(const juce::String& formatName, const juce::String& identifier) override;

    const juce::File& executable() const noexcept { return executable_; }

private:
    juce::File executable_;
    int timeoutMs_;
};

/// Scans in this process (tests, or when no scanner executable is available;
/// the dead man's pedal then protects against a crash loop).
class InProcessScanWorker : public PluginScanWorker {
public:
    explicit InProcessScanWorker(juce::AudioPluginFormatManager& formats) : formats_(formats) {}
    PluginScanResult scan(const juce::String& formatName, const juce::String& identifier) override;

private:
    juce::AudioPluginFormatManager& formats_;
};

/// Drives a scan over the formats of a format manager, recording results and
/// failures in a PluginScanCache. One thread at a time (the message thread, or
/// the PluginScanJob worker with its own staging cache).
class PluginScanner {
public:
    struct Report {
        int scanned{0};                         // identifiers handed to the worker
        int skipped{0};                         // cached and up to date, or blacklisted
        int added{0};                           // descriptions added or refreshed
        std::vector<juce::String> blacklisted;  // newly blacklisted this run
        bool cancelled{false};                  // stopped early by ShouldCancel
    };
    /// Called before each identifier is scanned (index, total, identifier).
    /// For scanAll, index / total run over all formats together.
    using Progress = std::function<void(int, int, const juce::String&)>;
    /// Polled between identifiers (never mid-scan: a plugin that is being
    /// scanned finishes or times out first). Return true to stop; what has
    /// been scanned so far is kept and saved.
    using ShouldCancel = std::function<bool()>;

    PluginScanner(juce::AudioPluginFormatManager& formats, PluginScanCache& cache, PluginScanWorker& worker);

    /// Candidate identifiers of one format: its default search locations
    /// (for AudioUnits, the registered components; listing does not load them).
    std::vector<juce::String> candidates(juce::AudioPluginFormat& format) const;

    /// Scan every candidate of every format. Up-to-date cache entries and
    /// blacklisted identifiers are skipped unless `rescanAll`. Saves the cache.
    Report scanAll(bool rescanAll = false, const Progress& progress = {}, const ShouldCancel& cancel = {});
    /// Scan the given identifiers of one format (same rules, saves the cache).
    Report scanIdentifiers(const juce::String& formatName, const std::vector<juce::String>& identifiers,
                           bool rescanAll = false, const Progress& progress = {},
                           const ShouldCancel& cancel = {});

    /// The format that claims `pluginId` (fileMightContainThisPluginType), or null.
    juce::AudioPluginFormat* formatFor(const juce::String& pluginId) const;

private:
    Report scanList(const juce::String& formatName, const std::vector<juce::String>& identifiers,
                    bool rescanAll, const Progress& progress, const ShouldCancel& cancel, int indexOffset,
                    int grandTotal);
    bool needsScan(juce::AudioPluginFormat& format, const juce::String& id, bool rescanAll) const;
    void scanOne(const juce::String& formatName, const juce::String& id, Report& report);

    juce::AudioPluginFormatManager& formats_;
    PluginScanCache& cache_;
    PluginScanWorker& worker_;
};

}  // namespace spacer
