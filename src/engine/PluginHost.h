#pragma once

#include <juce_audio_processors_headless/juce_audio_processors_headless.h>

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/InsertParamPath.h"
#include "core/PluginSlot.h"
#include "core/Project.h"
#include "engine/HostedPlugin.h"
#include "engine/InsertChain.h"
#include "engine/PluginScanCache.h"
#include "engine/PluginScanner.h"

namespace spacer {

/// Message-thread side of plugin hosting (SPEC §13): plugin formats, the
/// scan cache and scanner, and the instances in the project's insert slots.
///
/// GraphBuilder asks buildChain() for every source and speaker. Instances
/// are kept in a registry keyed by (owner, id, slot) and the state they hold,
/// so rebuilding a graph (a source added, a scene recalled, ...) reuses the
/// running instance of an unchanged slot instead of reloading the plugin
/// (SPEC §5.2: plugin instances survive SwapGraph). A slot whose plugin or
/// state changed gets a new instance, and the engine crossfades to it.
///
/// - A plugin that cannot be found or loaded leaves its slot bypassed and adds
///   a warning (takeWarnings()); the project still loads (SPEC §13).
/// - captureState() writes the instances' live state back into the model
///   (scene capture, saving, and before a recall so the diff sees edits made in
///   plugin editors).
/// - Latency changes reported by plugins are flagged (takeLatencyChanged());
///   the owner rebuilds the graph so the new latency is compensated.
///
/// Not thread-safe; the audio thread never touches this class.
class PluginHost {
public:
    struct Options {
        /// Where the scan cache lives (the app's support directory).
        juce::File cacheDirectory;
        /// Out-of-process scanner (spacer-plugin-scanner). If it does not
        /// exist, scans run in process (protected by the dead man's pedal).
        juce::File scannerExecutable;
        int scanTimeoutMs{30000};
        /// Scan an identifier missing from the cache when a project asks for it.
        bool scanUnknownOnDemand{true};

        /// The app's defaults: cache in <user app data>/Spacer, scanner next
        /// to the running executable (Spacer.app/Contents/MacOS).
        static Options appDefaults();
    };
    using FormatSetup = std::function<void(juce::AudioPluginFormatManager&)>;

    /// `setup` registers the plugin formats; the default is
    /// addPlatformFormats (headless AudioUnits on macOS, nothing elsewhere).
    /// The app passes juce::addDefaultFormatsToManager so plugins get editors.
    explicit PluginHost(Options options, FormatSetup setup = addPlatformFormats);
    ~PluginHost();
    PluginHost(const PluginHost&) = delete;
    PluginHost& operator=(const PluginHost&) = delete;

    static void addPlatformFormats(juce::AudioPluginFormatManager& formats);

    juce::AudioPluginFormatManager& formats() noexcept { return formats_; }
    PluginScanCache& cache() noexcept { return cache_; }
    PluginScanner& scanner() noexcept { return *scanner_; }
    bool scansOutOfProcess() const noexcept { return outOfProcess_; }

    /// Scan every format's candidates (see PluginScanner::scanAll).
    PluginScanner::Report scanAll(bool rescanAll = false, const PluginScanner::Progress& progress = {});

    /// The description for a slot's pluginId: from the cache, or (option
    /// scanUnknownOnDemand) by scanning just that identifier. nullopt if no
    /// format claims it, it is blacklisted, or the scan finds nothing.
    std::optional<juce::PluginDescription> resolve(const std::string& pluginId);

    /// Builds the insert chain of one source (numChannels 1 / 2) or speaker
    /// (1). Not prepared (InsertStage does that). Missing plugins become
    /// pass-through slots and add a warning.
    std::unique_ptr<InsertChain> buildChain(InsertOwner owner, int ownerId,
                                            const std::vector<PluginSlot>& slots, int numChannels,
                                            double sampleRate, int maxBlockSize);

    /// Writes every live instance's state (stateBase64, and the current values
    /// of the params already listed in the slot) into the project's sources
    /// and speaker chains. Returns the number of slots updated.
    int captureState(Project& project);

    /// The newest live instance in a slot (editors, WP11e), or null.
    std::shared_ptr<HostedPlugin> instanceFor(InsertOwner owner, int ownerId, int slot) const;
    /// Live instances in the registry (expired ones are pruned).
    int numLiveInstances();

    std::vector<std::string> takeWarnings();
    /// True (once) if any instance reported a latency change since the last call.
    bool takeLatencyChanged() noexcept { return latencyFlag_->exchange(false, std::memory_order_acq_rel); }

private:
    struct Entry {
        InsertOwner owner{InsertOwner::Source};
        int ownerId{0};
        int slot{0};
        std::string pluginId;
        std::string state;            // the state the instance is known to hold
        std::map<int, float> params;  // params last applied / read back
        int numChannels{1};
        double sampleRate{0.0};
        int maxBlockSize{0};
        std::weak_ptr<HostedPlugin> plugin;
    };

    std::shared_ptr<HostedPlugin> reuse(InsertOwner owner, int ownerId, int slot, const PluginSlot& spec,
                                        int numChannels, double sampleRate, int maxBlockSize);
    std::shared_ptr<HostedPlugin> load(InsertOwner owner, int ownerId, int slot, const PluginSlot& spec,
                                       int numChannels, double sampleRate, int maxBlockSize);
    Entry* newestEntry(InsertOwner owner, int ownerId, int slot, const std::string& pluginId);
    void captureSlots(InsertOwner owner, int ownerId, std::vector<PluginSlot>& slots, int& updated);
    void prune();
    void warn(std::string message);

    Options options_;
    juce::AudioPluginFormatManager formats_;
    PluginScanCache cache_;
    std::unique_ptr<PluginScanWorker> worker_;
    std::unique_ptr<PluginScanner> scanner_;
    bool outOfProcess_{false};
    std::vector<Entry> entries_;
    std::vector<std::string> warnings_;
    std::shared_ptr<std::atomic<bool>> latencyFlag_;
};

}  // namespace spacer
