#include "engine/PluginScanCache.h"

#include <algorithm>

namespace spacer {

namespace {
constexpr int kCacheVersion = 1;
const char* const kRoot = "SPACER_PLUGIN_CACHE";
}  // namespace

PluginScanCache::PluginScanCache(juce::File directory) : directory_(std::move(directory)) {}

juce::File PluginScanCache::cacheFile() const {
    return directory_.getChildFile("plugin-cache.xml");
}

juce::File PluginScanCache::pedalFile() const {
    return directory_.getChildFile("plugin-scan.pedal");
}

bool PluginScanCache::load() {
    known_.clear();
    blacklist_.clear();
    recoveredCrash_.reset();
    bool ok = true;

    const juce::File file = cacheFile();
    if (file.existsAsFile()) {
        const auto xml = juce::XmlDocument::parse(file);
        if (xml == nullptr || !xml->hasTagName(kRoot) || xml->getIntAttribute("version") != kCacheVersion) {
            ok = false;
        } else {
            if (const auto* known = xml->getChildByName("KNOWN")) {
                for (const auto* e : known->getChildIterator()) {
                    juce::PluginDescription d;
                    if (d.loadFromXml(*e)) known_.push_back(d);
                }
            }
            if (const auto* bl = xml->getChildByName("BLACKLIST")) {
                for (const auto* e : bl->getChildWithTagNameIterator("ENTRY"))
                    blacklist_.push_back({e->getStringAttribute("id"), e->getStringAttribute("reason")});
            }
        }
    }

    const juce::File pedal = pedalFile();
    if (pedal.existsAsFile()) {
        const juce::String id = pedal.loadFileAsString().trim();
        pedal.deleteFile();
        if (id.isNotEmpty()) {
            addToBlacklist(id, "crashed during a previous scan");
            recoveredCrash_ = id;
            save();
        }
    }
    return ok;
}

bool PluginScanCache::save() const {
    juce::XmlElement root(kRoot);
    root.setAttribute("version", kCacheVersion);
    auto* known = root.createNewChildElement("KNOWN");
    for (const auto& d : known_) known->addChildElement(d.createXml().release());
    auto* bl = root.createNewChildElement("BLACKLIST");
    for (const auto& b : blacklist_) {
        auto* e = bl->createNewChildElement("ENTRY");
        e->setAttribute("id", b.identifier);
        e->setAttribute("reason", b.reason);
    }
    if (!directory_.createDirectory()) return false;
    return root.writeTo(cacheFile());
}

void PluginScanCache::add(const juce::PluginDescription& d) {
    known_.erase(std::remove_if(known_.begin(), known_.end(),
                                [&](const juce::PluginDescription& k) {
                                    return k.pluginFormatName == d.pluginFormatName &&
                                           k.fileOrIdentifier == d.fileOrIdentifier &&
                                           k.uniqueId == d.uniqueId;
                                }),
                 known_.end());
    known_.push_back(d);
}

void PluginScanCache::removeIdentifier(const juce::String& id) {
    known_.erase(std::remove_if(known_.begin(), known_.end(),
                                [&](const juce::PluginDescription& k) { return k.fileOrIdentifier == id; }),
                 known_.end());
}

std::optional<juce::PluginDescription> PluginScanCache::find(const juce::String& pluginId) const {
    for (const auto& d : known_)
        if (d.fileOrIdentifier == pluginId) return d;
    for (const auto& d : known_)
        if (d.createIdentifierString() == pluginId) return d;
    return std::nullopt;
}

bool PluginScanCache::hasIdentifier(const juce::String& id) const {
    return std::any_of(known_.begin(), known_.end(),
                       [&](const juce::PluginDescription& d) { return d.fileOrIdentifier == id; });
}

bool PluginScanCache::isBlacklisted(const juce::String& id) const {
    return std::any_of(blacklist_.begin(), blacklist_.end(),
                       [&](const BlacklistEntry& b) { return b.identifier == id; });
}

void PluginScanCache::addToBlacklist(const juce::String& id, const juce::String& reason) {
    for (auto& b : blacklist_)
        if (b.identifier == id) {
            b.reason = reason;
            return;
        }
    blacklist_.push_back({id, reason});
}

void PluginScanCache::removeFromBlacklist(const juce::String& id) {
    blacklist_.erase(std::remove_if(blacklist_.begin(), blacklist_.end(),
                                    [&](const BlacklistEntry& b) { return b.identifier == id; }),
                     blacklist_.end());
}

void PluginScanCache::beginScan(const juce::String& id) {
    directory_.createDirectory();
    pedalFile().replaceWithText(id);
}

void PluginScanCache::endScan() {
    pedalFile().deleteFile();
}

}  // namespace spacer
