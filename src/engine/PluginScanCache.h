#pragma once

#include <juce_audio_processors_headless/juce_audio_processors_headless.h>

#include <optional>
#include <string>
#include <vector>

namespace spacer {

/// Results of plugin scanning, kept in the app's support directory between
/// runs (SPEC §13), plus the bookkeeping that keeps a crashing plugin from
/// being scanned again and again:
///
/// - **Blacklist**: identifiers whose scan crashed, hung or failed; they are
///   skipped until removed (e.g. "rescan" in the UI).
/// - **Dead man's pedal**: before each scan the identifier is written to a
///   pedal file and removed afterwards. If the pedal is still there when the
///   cache is loaded, the scanning process died on that plugin (for an
///   in-process scan, the app itself crashed) and it is blacklisted.
///
/// Files in `directory`: plugin-cache.xml, plugin-scan.pedal. Message thread
/// only (not thread-safe).
class PluginScanCache {
public:
    struct BlacklistEntry {
        juce::String identifier;
        juce::String reason;
    };

    explicit PluginScanCache(juce::File directory);

    const juce::File& directory() const noexcept { return directory_; }
    juce::File cacheFile() const;
    juce::File pedalFile() const;

    /// Reads the cache file (a missing file is an empty cache) and recovers a
    /// crashed scan from the pedal. False if the file exists but is unreadable
    /// (the cache then starts empty).
    bool load();
    bool save() const;

    // ---- known plugins ----
    const std::vector<juce::PluginDescription>& known() const noexcept { return known_; }
    /// Replaces any entry with the same format + fileOrIdentifier + uniqueId.
    void add(const juce::PluginDescription& description);
    /// Removes every entry of that identifier (one file may hold several plugins).
    void removeIdentifier(const juce::String& fileOrIdentifier);
    /// By fileOrIdentifier, or by PluginDescription::createIdentifierString().
    std::optional<juce::PluginDescription> find(const juce::String& pluginId) const;
    bool hasIdentifier(const juce::String& fileOrIdentifier) const;

    // ---- blacklist ----
    const std::vector<BlacklistEntry>& blacklist() const noexcept { return blacklist_; }
    bool isBlacklisted(const juce::String& identifier) const;
    void addToBlacklist(const juce::String& identifier, const juce::String& reason);
    void removeFromBlacklist(const juce::String& identifier);

    // ---- dead man's pedal ----
    /// Writes the pedal (do this immediately before scanning `identifier`).
    void beginScan(const juce::String& identifier);
    /// Removes the pedal (scan finished, whatever the outcome).
    void endScan();
    /// The identifier recovered from a pedal by the last load(), if any.
    const std::optional<juce::String>& recoveredCrash() const noexcept { return recoveredCrash_; }

private:
    juce::File directory_;
    std::vector<juce::PluginDescription> known_;
    std::vector<BlacklistEntry> blacklist_;
    std::optional<juce::String> recoveredCrash_;
};

}  // namespace spacer
