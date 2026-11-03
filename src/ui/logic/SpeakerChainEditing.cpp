#include "ui/logic/SpeakerChainEditing.h"

#include <algorithm>

namespace spacer::chainedit {

namespace {

/// Scoped edit of one speaker's chain: apply sets / erases it, revert
/// restores what was there. Nothing else in the project is touched.
ProjectEdit setChain(std::optional<SpeakerChainSpec> before, std::optional<SpeakerChainSpec> after,
                     int speakerId, std::string name) {
    const auto put = [speakerId](Project& p, const std::optional<SpeakerChainSpec>& chain) {
        auto it = std::find_if(p.speakerChains.begin(), p.speakerChains.end(),
                               [&](const SpeakerChainSpec& c) { return c.speakerId == speakerId; });
        if (!chain) {
            if (it != p.speakerChains.end()) p.speakerChains.erase(it);
        } else if (it != p.speakerChains.end()) {
            *it = *chain;
        } else {
            p.speakerChains.push_back(*chain);
        }
    };
    ProjectEdit e;
    e.name = std::move(name);
    e.apply = [put, after = std::move(after)](Project& p) { put(p, after); };
    e.revert = [put, before = std::move(before)](Project& p) { put(p, before); };
    e.effect = EngineEffect::structural();
    return e;
}

SpeakerChainSpec chainFor(const Project& project, const Speaker& speaker) {
    if (const auto* c = findChain(project, speaker.id)) return *c;
    SpeakerChainSpec chain;
    chain.speakerId = speaker.id;
    chain.trimDb = speaker.trimDb;
    chain.userDelayMs = speaker.userDelayMs;
    chain.polarityInverted = speaker.polarityInverted;
    return chain;
}

template <typename Fn>
std::optional<ProjectEdit> editInserts(const Project& project, int speakerId, Fn&& mutate, std::string name) {
    const Speaker* speaker = project.layout.findSpeaker(speakerId);
    if (speaker == nullptr) return std::nullopt;
    SpeakerChainSpec after = chainFor(project, *speaker);
    const auto* existing = findChain(project, speakerId);
    if (!mutate(after.inserts)) return std::nullopt;
    std::optional<SpeakerChainSpec> before;
    if (existing != nullptr) before = *existing;
    if (before && *before == after) return std::nullopt;
    std::optional<SpeakerChainSpec> next = after;
    if (after.inserts.empty() && isDefaultChain(after, *speaker)) next.reset();
    return setChain(std::move(before), std::move(next), speakerId, std::move(name));
}

}  // namespace

const SpeakerChainSpec* findChain(const Project& project, int speakerId) {
    for (const auto& c : project.speakerChains)
        if (c.speakerId == speakerId) return &c;
    return nullptr;
}

std::vector<insertedit::InsertRow> insertRows(const Project& project, int speakerId) {
    const auto* c = findChain(project, speakerId);
    return c != nullptr ? insertedit::rows(c->inserts) : std::vector<insertedit::InsertRow>{};
}

bool isDefaultChain(const SpeakerChainSpec& chain, const Speaker& speaker) {
    const SpeakerChainSpec plain = [&] {
        SpeakerChainSpec c;
        c.speakerId = speaker.id;
        c.trimDb = speaker.trimDb;
        c.userDelayMs = speaker.userDelayMs;
        c.polarityInverted = speaker.polarityInverted;
        return c;
    }();
    return chain == plain;
}

std::optional<ProjectEdit> editAddInsert(const Project& project, int speakerId, const std::string& pluginId) {
    if (pluginId.empty()) return std::nullopt;
    return editInserts(
        project, speakerId,
        [&](std::vector<PluginSlot>& inserts) {
            if (static_cast<int>(inserts.size()) >= kMaxInserts) return false;
            PluginSlot slot;
            slot.pluginId = pluginId;
            inserts.push_back(slot);
            return true;
        },
        "Add Speaker Insert");
}

std::optional<ProjectEdit> editRemoveInsert(const Project& project, int speakerId, int slot) {
    return editInserts(
        project, speakerId,
        [&](std::vector<PluginSlot>& inserts) {
            if (slot < 0 || slot >= static_cast<int>(inserts.size())) return false;
            inserts.erase(inserts.begin() + slot);
            return true;
        },
        "Remove Speaker Insert");
}

std::optional<ProjectEdit> editInsertBypass(const Project& project, int speakerId, int slot, bool bypassed) {
    auto edit = editInserts(
        project, speakerId,
        [&](std::vector<PluginSlot>& inserts) {
            if (slot < 0 || slot >= static_cast<int>(inserts.size())) return false;
            inserts[static_cast<std::size_t>(slot)].bypassed = bypassed;
            return true;
        },
        bypassed ? "Bypass Speaker Insert" : "Enable Speaker Insert");
    if (edit) edit->effect = EngineEffect::speakerInsertBypass(speakerId, slot);  // a flag, not a new graph
    return edit;
}

}  // namespace spacer::chainedit
