#include "engine/PluginScanner.h"

#include <algorithm>

#include "engine/PluginScanProcess.h"

namespace spacer {

// ---- workers -------------------------------------------------------------------

ChildProcessScanWorker::ChildProcessScanWorker(juce::File executable, int timeoutMs)
    : executable_(std::move(executable)), timeoutMs_(std::max(100, timeoutMs)) {}

PluginScanResult ChildProcessScanWorker::scan(const juce::String& formatName,
                                              const juce::String& identifier) {
    PluginScanResult r;
    if (!executable_.existsAsFile()) {
        r.message = "scanner not found: " + executable_.getFullPathName().toStdString();
        return r;
    }
    const juce::File resultFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                      .getNonexistentChildFile("spacer-scan", ".xml", false);
    juce::ChildProcess child;
    const juce::StringArray args{executable_.getFullPathName(), "--scan", formatName, identifier,
                                 resultFile.getFullPathName()};
    if (!child.start(args, 0)) {
        r.message = "could not start the scanner";
        return r;
    }
    if (!child.waitForProcessToFinish(timeoutMs_)) {
        child.kill();
        resultFile.deleteFile();
        r.status = PluginScanResult::Status::TimedOut;
        r.message = "scan timed out after " + std::to_string(timeoutMs_) + " ms";
        return r;
    }
    bool present = false;
    r = readScanResult(resultFile, present);
    resultFile.deleteFile();
    resultFile.getSiblingFile(resultFile.getFileName() + ".part").deleteFile();
    if (!present) {
        r.status = PluginScanResult::Status::Crashed;
        r.message = "scanner exited without a result (crashed)";
    }
    return r;
}

PluginScanResult InProcessScanWorker::scan(const juce::String& formatName, const juce::String& identifier) {
    return scanInProcess(formats_, formatName, identifier);
}

// ---- scanner -------------------------------------------------------------------

PluginScanner::PluginScanner(juce::AudioPluginFormatManager& formats, PluginScanCache& cache,
                             PluginScanWorker& worker)
    : formats_(formats), cache_(cache), worker_(worker) {}

std::vector<juce::String> PluginScanner::candidates(juce::AudioPluginFormat& format) const {
    std::vector<juce::String> out;
    if (!format.canScanForPlugins()) return out;
    for (const auto& id : format.searchPathsForPlugins(format.getDefaultLocationsToSearch(), true, false))
        out.push_back(id);
    return out;
}

juce::AudioPluginFormat* PluginScanner::formatFor(const juce::String& pluginId) const {
    for (auto* f : formats_.getFormats())
        if (f->fileMightContainThisPluginType(pluginId)) return f;
    return nullptr;
}

bool PluginScanner::needsScan(juce::AudioPluginFormat& format, const juce::String& id, bool rescanAll) const {
    if (rescanAll) return true;
    if (cache_.isBlacklisted(id)) return false;
    bool cached = false;
    for (const auto& d : cache_.known()) {
        if (d.fileOrIdentifier != id || d.pluginFormatName != format.getName()) continue;
        cached = true;
        if (format.pluginNeedsRescanning(d)) return true;
    }
    return !cached;
}

void PluginScanner::scanOne(const juce::String& formatName, const juce::String& id, Report& report) {
    ++report.scanned;
    cache_.beginScan(id);
    const PluginScanResult r = worker_.scan(formatName, id);
    cache_.endScan();

    if (r.status == PluginScanResult::Status::Ok) {
        cache_.removeFromBlacklist(id);
        cache_.removeIdentifier(id);
        for (const auto& d : r.found) {
            cache_.add(d);
            ++report.added;
        }
        return;
    }
    const char* what = r.status == PluginScanResult::Status::Crashed    ? "crashed"
                       : r.status == PluginScanResult::Status::TimedOut ? "timed out"
                                                                        : "failed";
    cache_.removeIdentifier(id);
    cache_.addToBlacklist(id, juce::String(what) + ": " + juce::String(r.message));
    report.blacklisted.push_back(id);
}

PluginScanner::Report PluginScanner::scanIdentifiers(const juce::String& formatName,
                                                     const std::vector<juce::String>& identifiers,
                                                     bool rescanAll, const Progress& progress) {
    Report report;
    juce::AudioPluginFormat* format = nullptr;
    for (auto* f : formats_.getFormats())
        if (f->getName() == formatName) format = f;
    if (format == nullptr) return report;

    const int total = static_cast<int>(identifiers.size());
    for (int i = 0; i < total; ++i) {
        const juce::String& id = identifiers[static_cast<std::size_t>(i)];
        if (!needsScan(*format, id, rescanAll)) {
            ++report.skipped;
            continue;
        }
        if (progress) progress(i, total, id);
        scanOne(formatName, id, report);
    }
    cache_.save();
    return report;
}

PluginScanner::Report PluginScanner::scanAll(bool rescanAll, const Progress& progress) {
    Report total;
    for (auto* f : formats_.getFormats()) {
        const Report r = scanIdentifiers(f->getName(), candidates(*f), rescanAll, progress);
        total.scanned += r.scanned;
        total.skipped += r.skipped;
        total.added += r.added;
        total.blacklisted.insert(total.blacklisted.end(), r.blacklisted.begin(), r.blacklisted.end());
    }
    cache_.save();
    return total;
}

}  // namespace spacer
