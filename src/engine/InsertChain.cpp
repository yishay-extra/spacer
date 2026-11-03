#include "engine/InsertChain.h"

#include <algorithm>
#include <atomic>

namespace spacer {

namespace {
std::atomic<std::uint64_t> nextChainId{1};
const std::string kNoId;
}  // namespace

InsertChain::InsertChain() : id_(nextChainId.fetch_add(1, std::memory_order_relaxed)) {}

void InsertChain::addSlot(std::shared_ptr<HostedPlugin> plugin, std::string pluginId, bool bypassed) {
    if (numSlots_ >= kMaxInserts) return;
    Slot& s = slots_[static_cast<std::size_t>(numSlots_++)];
    s.latency = plugin ? plugin->latencySamples() : 0;
    latency_ += s.latency;
    s.plugin = std::move(plugin);
    s.pluginId = std::move(pluginId);
    s.bypassed = bypassed;
}

void InsertChain::prepare(double sampleRate, int maxBlockSize, int numChannels, int padCapacity) {
    numChannels_ = std::clamp(numChannels, 1, 2);
    maxBlockSize_ = std::max(1, maxBlockSize);
    int bufferChannels = numChannels_;
    for (int i = 0; i < numSlots_; ++i) {
        Slot& s = slots_[static_cast<std::size_t>(i)];
        if (s.plugin) bufferChannels = std::max(bufferChannels, s.plugin->bufferChannels());
        s.bypassMix.prepare(sampleRate, s.bypassed ? 1.0f : 0.0f);
        s.dry.prepare(
            {sampleRate, static_cast<juce::uint32>(maxBlockSize_), static_cast<juce::uint32>(numChannels_)});
        s.dry.setMaximumDelayInSamples(std::max(1, s.latency + 1));
        s.dry.reset();
    }
    io_.setSize(bufferChannels, maxBlockSize_);
    io_.clear();
    dry_.setSize(numChannels_, maxBlockSize_);
    dry_.clear();
    midi_.ensureSize(2048);
    pad_.prepare(sampleRate, maxBlockSize_, numChannels_, padCapacity);
}

HostedPlugin* InsertChain::plugin(int slot) const noexcept {
    if (slot < 0 || slot >= numSlots_) return nullptr;
    return slots_[static_cast<std::size_t>(slot)].plugin.get();
}

const std::string& InsertChain::pluginId(int slot) const noexcept {
    if (slot < 0 || slot >= numSlots_) return kNoId;
    return slots_[static_cast<std::size_t>(slot)].pluginId;
}

bool InsertChain::isBypassed(int slot) const noexcept {
    return slot >= 0 && slot < numSlots_ && slots_[static_cast<std::size_t>(slot)].bypassed;
}

bool InsertChain::sameInstancesAs(const InsertChain& other) const noexcept {
    if (numSlots_ != other.numSlots_ || numChannels_ != other.numChannels_) return false;
    for (int i = 0; i < numSlots_; ++i) {
        const Slot& a = slots_[static_cast<std::size_t>(i)];
        const Slot& b = other.slots_[static_cast<std::size_t>(i)];
        if (a.plugin != b.plugin || a.pluginId != b.pluginId) return false;
    }
    return true;
}

void InsertChain::claim() noexcept {
    for (int i = 0; i < numSlots_; ++i)
        if (auto* p = slots_[static_cast<std::size_t>(i)].plugin.get()) p->setOwner(id_);
}

void InsertChain::setBypassed(int slot, bool bypassed) noexcept {
    if (slot < 0 || slot >= numSlots_) return;
    Slot& s = slots_[static_cast<std::size_t>(slot)];
    s.bypassed = bypassed;
    s.bypassMix.setTarget(bypassed ? 1.0f : 0.0f, kBypassFadeMs);
}

bool InsertChain::setParameter(int slot, int index, float value) noexcept {
    HostedPlugin* p = plugin(slot);
    return p != nullptr && p->setParameter(index, value);
}

float InsertChain::parameter(int slot, int index) const noexcept {
    const HostedPlugin* p = plugin(slot);
    return p != nullptr ? p->parameter(index) : 0.0f;
}

void InsertChain::adoptSettingsFrom(const InsertChain& other) noexcept {
    for (int i = 0; i < std::min(numSlots_, other.numSlots_); ++i) {
        const bool b = other.slots_[static_cast<std::size_t>(i)].bypassed;
        if (b != slots_[static_cast<std::size_t>(i)].bypassed) setBypassed(i, b);
    }
}

void InsertChain::processSlot(Slot& s, float* const* ch, int numChannels, int n) noexcept {
    HostedPlugin* p = s.plugin.get();
    if (p == nullptr) return;  // missing plugin: pass through, no latency

    // Latency-aligned copy of the input (always kept up to date so a bypass
    // or a lost claim can switch to it at any time).
    for (int c = 0; c < numChannels; ++c) {
        float* d = dry_.getWritePointer(c);
        if (s.latency == 0) {
            std::copy(ch[c], ch[c] + n, d);
        } else {
            for (int i = 0; i < n; ++i) {
                s.dry.pushSample(c, ch[c][i]);
                d[i] = s.dry.popSample(c, static_cast<float>(s.latency));
            }
        }
    }

    if (p->owner() == 0) p->setOwner(id_);
    if (p->owner() != id_) {
        for (int c = 0; c < numChannels; ++c)
            std::copy(dry_.getReadPointer(c), dry_.getReadPointer(c) + n, ch[c]);
        return;
    }

    // Plugin process buffer: our channels in, a mono signal duplicated into
    // a stereo-only plugin, unused channels silent; channel 0 (or 0/1) out.
    const int bufferChannels = io_.getNumChannels();
    for (int c = 0; c < bufferChannels; ++c) {
        float* dst = io_.getWritePointer(c);
        if (c < numChannels)
            std::copy(ch[c], ch[c] + n, dst);
        else if (numChannels == 1 && c == 1)
            std::copy(ch[0], ch[0] + n, dst);
        else
            std::fill(dst, dst + n, 0.0f);
    }
    juce::AudioBuffer<float> view(io_.getArrayOfWritePointers(), bufferChannels, n);
    midi_.clear();
    p->instance().processBlock(view, midi_);
    for (int c = 0; c < numChannels; ++c) std::copy(io_.getReadPointer(c), io_.getReadPointer(c) + n, ch[c]);

    // Bypass crossfade towards the aligned input.
    if (s.bypassMix.isRamping()) {
        for (int i = 0; i < n; ++i) {
            const float m = s.bypassMix.next();
            for (int c = 0; c < numChannels; ++c) {
                const float dry = dry_.getSample(c, i);
                ch[c][i] += (dry - ch[c][i]) * m;
            }
        }
    } else if (s.bypassMix.current() >= 1.0f) {
        for (int c = 0; c < numChannels; ++c)
            std::copy(dry_.getReadPointer(c), dry_.getReadPointer(c) + n, ch[c]);
    }
}

void InsertChain::process(float* const* ch, int numChannels, int n) noexcept {
    numChannels = std::min(numChannels, numChannels_);
    n = std::min(n, maxBlockSize_);
    for (int i = 0; i < numSlots_; ++i) processSlot(slots_[static_cast<std::size_t>(i)], ch, numChannels, n);
    pad_.process(ch, numChannels, n);
}

}  // namespace spacer
