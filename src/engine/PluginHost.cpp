#include "engine/PluginHost.h"

#include <algorithm>

#include "engine/PluginState.h"

namespace spacer {

namespace {
const char* ownerName(InsertOwner o) {
    return o == InsertOwner::Source ? "source" : "speaker";
}
}  // namespace

PluginHost::Options PluginHost::Options::appDefaults() {
    Options o;
    o.cacheDirectory =
        juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("Spacer");
#if JUCE_MAC
    o.cacheDirectory = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                           .getChildFile("Application Support/Spacer");
#endif
    o.scannerExecutable = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                              .getSiblingFile("spacer-plugin-scanner");
    return o;
}

PluginHost::PluginHost(Options options, FormatSetup setup)
    : options_(std::move(options)),
      cache_(options_.cacheDirectory),
      latencyFlag_(std::make_shared<std::atomic<bool>>(false)) {
    if (setup) setup(formats_);
    cache_.load();
    if (const auto& crashed = cache_.recoveredCrash())
        warn("plugin scan of '" + crashed->toStdString() + "' crashed last time; it has been blacklisted");
    if (options_.scannerExecutable.existsAsFile()) {
        worker_ =
            std::make_unique<ChildProcessScanWorker>(options_.scannerExecutable, options_.scanTimeoutMs);
        outOfProcess_ = true;
    } else {
        worker_ = std::make_unique<InProcessScanWorker>(formats_);
    }
    scanner_ = std::make_unique<PluginScanner>(formats_, cache_, *worker_);
}

PluginHost::~PluginHost() = default;

void PluginHost::addPlatformFormats(juce::AudioPluginFormatManager& formats) {
#if JUCE_PLUGINHOST_AU && JUCE_MAC
    formats.addFormat(std::make_unique<juce::AudioUnitPluginFormatHeadless>());
#else
    juce::ignoreUnused(formats);
#endif
}

PluginScanner::Report PluginHost::scanAll(bool rescanAll, const PluginScanner::Progress& progress,
                                          const PluginScanner::ShouldCancel& cancel) {
    return scanner_->scanAll(rescanAll, progress, cancel);
}

void PluginHost::adoptScanCache(PluginScanCache&& scanned) {
    cache_ = std::move(scanned);
}

std::optional<juce::PluginDescription> PluginHost::resolve(const std::string& pluginId) {
    const juce::String id(pluginId);
    if (auto d = cache_.find(id)) return d;
    if (cache_.isBlacklisted(id) || !options_.scanUnknownOnDemand || backgroundScan_.load())
        return std::nullopt;
    auto* format = scanner_->formatFor(id);
    if (format == nullptr) return std::nullopt;
    scanner_->scanIdentifiers(format->getName(), {id});
    return cache_.find(id);
}

void PluginHost::warn(std::string message) {
    warnings_.push_back(std::move(message));
}

std::vector<std::string> PluginHost::takeWarnings() {
    return std::exchange(warnings_, {});
}

void PluginHost::prune() {
    entries_.erase(
        std::remove_if(entries_.begin(), entries_.end(), [](const Entry& e) { return e.plugin.expired(); }),
        entries_.end());
}

int PluginHost::numLiveInstances() {
    prune();
    return static_cast<int>(entries_.size());
}

std::shared_ptr<HostedPlugin> PluginHost::reuse(InsertOwner owner, int ownerId, int slot,
                                                const PluginSlot& spec, int numChannels, double sampleRate,
                                                int maxBlockSize) {
    for (auto it = entries_.rbegin(); it != entries_.rend(); ++it) {
        Entry& e = *it;
        if (e.owner != owner || e.ownerId != ownerId || e.slot != slot || e.pluginId != spec.pluginId ||
            e.state != spec.stateBase64 || e.numChannels != numChannels ||
            !juce::exactlyEqual(e.sampleRate, sampleRate) || e.maxBlockSize != maxBlockSize)
            continue;
        auto plugin = e.plugin.lock();
        if (!plugin) continue;
        // Only parameters the model changed since they were last applied are
        // pushed, so edits made in the plugin's editor survive a rebuild.
        std::map<int, float> changed;
        for (const auto& [index, value] : spec.params) {
            const auto p = e.params.find(index);
            if (p == e.params.end() || !juce::exactlyEqual(p->second, value)) changed[index] = value;
        }
        pluginstate::applyParams(plugin->instance(), changed);
        for (const auto& [index, value] : changed) e.params[index] = value;
        return plugin;
    }
    return nullptr;
}

std::shared_ptr<HostedPlugin> PluginHost::load(InsertOwner owner, int ownerId, int slot,
                                               const PluginSlot& spec, int numChannels, double sampleRate,
                                               int maxBlockSize) {
    const std::string where =
        std::string(ownerName(owner)) + " " + std::to_string(ownerId) + " insert " + std::to_string(slot + 1);
    const auto description = resolve(spec.pluginId);
    if (!description) {
        warn(where + ": plugin '" + spec.pluginId + "' not found; slot bypassed");
        return nullptr;
    }
    juce::String error;
    auto instance = formats_.createPluginInstance(*description, sampleRate, maxBlockSize, error);
    if (!instance) {
        warn(where + ": plugin '" + spec.pluginId + "' failed to load (" + error.toStdString() +
             "); slot bypassed");
        return nullptr;
    }
    std::string stateError;
    if (!pluginstate::restore(*instance, spec, &stateError))
        warn(where + ": " + stateError + "; plugin defaults used");
    auto plugin = std::make_shared<HostedPlugin>(std::move(instance), spec.pluginId, numChannels, sampleRate,
                                                 maxBlockSize, latencyFlag_);
    plugin->setEditedFlag(editedFlag_);
    Entry e;
    e.owner = owner;
    e.ownerId = ownerId;
    e.slot = slot;
    e.pluginId = spec.pluginId;
    e.state = spec.stateBase64;
    e.params = spec.params;
    e.numChannels = numChannels;
    e.sampleRate = sampleRate;
    e.maxBlockSize = maxBlockSize;
    e.plugin = plugin;
    entries_.push_back(std::move(e));
    return plugin;
}

std::unique_ptr<InsertChain> PluginHost::buildChain(InsertOwner owner, int ownerId,
                                                    const std::vector<PluginSlot>& slots, int numChannels,
                                                    double sampleRate, int maxBlockSize) {
    prune();
    auto chain = std::make_unique<InsertChain>();
    const int n = std::min(static_cast<int>(slots.size()), kMaxInserts);
    for (int i = 0; i < n; ++i) {
        const PluginSlot& spec = slots[static_cast<std::size_t>(i)];
        std::shared_ptr<HostedPlugin> plugin;
        if (!spec.pluginId.empty()) {
            plugin = reuse(owner, ownerId, i, spec, numChannels, sampleRate, maxBlockSize);
            if (!plugin) plugin = load(owner, ownerId, i, spec, numChannels, sampleRate, maxBlockSize);
        }
        chain->addSlot(std::move(plugin), spec.pluginId, spec.bypassed);
    }
    if (static_cast<int>(slots.size()) > kMaxInserts)
        warn(std::string(ownerName(owner)) + " " + std::to_string(ownerId) + ": only the first " +
             std::to_string(kMaxInserts) + " inserts are used");
    return chain;
}

PluginHost::Entry* PluginHost::newestEntry(InsertOwner owner, int ownerId, int slot,
                                           const std::string& pluginId) {
    for (auto it = entries_.rbegin(); it != entries_.rend(); ++it)
        if (it->owner == owner && it->ownerId == ownerId && it->slot == slot && it->pluginId == pluginId &&
            !it->plugin.expired())
            return &*it;
    return nullptr;
}

std::shared_ptr<HostedPlugin> PluginHost::instanceFor(InsertOwner owner, int ownerId, int slot) const {
    for (auto it = entries_.rbegin(); it != entries_.rend(); ++it)
        if (it->owner == owner && it->ownerId == ownerId && it->slot == slot)
            if (auto p = it->plugin.lock()) return p;
    return nullptr;
}

void PluginHost::captureSlots(InsertOwner owner, int ownerId, std::vector<PluginSlot>& slots, int& updated) {
    for (std::size_t i = 0; i < slots.size(); ++i) {
        PluginSlot& slot = slots[i];
        Entry* e = newestEntry(owner, ownerId, static_cast<int>(i), slot.pluginId);
        if (e == nullptr) continue;
        auto plugin = e->plugin.lock();
        slot.stateBase64 = pluginstate::capture(plugin->instance());
        slot.params = pluginstate::readParams(plugin->instance(), slot.params);
        e->state = slot.stateBase64;
        e->params = slot.params;
        ++updated;
    }
}

int PluginHost::captureState(Project& project) {
    prune();
    int updated = 0;
    for (auto& s : project.sources) captureSlots(InsertOwner::Source, s.id, s.inserts, updated);
    for (auto& c : project.speakerChains) captureSlots(InsertOwner::Speaker, c.speakerId, c.inserts, updated);
    return updated;
}

}  // namespace spacer
