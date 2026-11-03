#include "engine/InsertStage.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace spacer {

InsertStage::InsertStage(std::unique_ptr<InsertChain> chain)
    : current_(chain ? std::move(chain) : std::make_unique<InsertChain>()) {}

void InsertStage::prepare(double sampleRate, int maxBlockSize, int numChannels) {
    sampleRate_ = sampleRate;
    numChannels_ = std::clamp(numChannels, 1, 2);
    maxBlockSize_ = std::max(1, maxBlockSize);
    xfadeLength_ = std::max(1, static_cast<int>(std::lround(kCrossfadeMs * 0.001 * sampleRate)));
    xfadePos_ = -1;
    scratch_.setSize(numChannels_, maxBlockSize_);
    scratch_.clear();
    current_->prepare(sampleRate, maxBlockSize_, numChannels_, current_->padCapacity());
}

void InsertStage::configurePad(int padSamples, int padCapacity) {
    current_->prepare(sampleRate_, maxBlockSize_, numChannels_, std::max(padCapacity, padSamples));
    current_->resetPad(padSamples);
}

void InsertStage::retargetTo(InsertStage& fresh) noexcept {
    if (!current_ || !fresh.current_) return;
    InsertChain& next = *fresh.current_;
    const bool compatible = fresh.numChannels_ == numChannels_ && fresh.maxBlockSize_ == maxBlockSize_ &&
                            juce::exactlyEqual(fresh.sampleRate_, sampleRate_);
    if (compatible && current_->sameInstancesAs(next) && next.padSamples() <= current_->padCapacity()) {
        current_->adoptSettingsFrom(next);
        current_->setPadSamples(next.padSamples());
        current_->claim();
        return;
    }
    // Hand any stale outgoing chain to `fresh` (freed with the old graph), make
    // the running chain the outgoing one and `fresh`'s chain the current one.
    std::swap(outgoing_, fresh.outgoing_);
    std::swap(outgoing_, current_);
    std::swap(current_, fresh.current_);
    current_->claim();
    if (!compatible) {
        // Different format: the old chain cannot run here; take fresh's
        // scratch as well and switch without a crossfade.
        std::swap(outgoing_, fresh.outgoing_);
        std::swap(scratch_, fresh.scratch_);
        sampleRate_ = fresh.sampleRate_;
        numChannels_ = fresh.numChannels_;
        maxBlockSize_ = fresh.maxBlockSize_;
        xfadeLength_ = fresh.xfadeLength_;
        xfadePos_ = -1;
        return;
    }
    xfadePos_ = 0;
}

void InsertStage::process(float* const* ch, int numChannels, int n) noexcept {
    if (!current_) return;
    numChannels = std::min(numChannels, numChannels_);
    if (xfadePos_ < 0 || !outgoing_) {
        current_->process(ch, numChannels, n);
        return;
    }
    float* old[2] = {scratch_.getWritePointer(0), scratch_.getWritePointer(numChannels_ - 1)};
    for (int c = 0; c < numChannels; ++c) std::copy(ch[c], ch[c] + n, old[c]);
    outgoing_->process(old, numChannels, n);
    current_->process(ch, numChannels, n);
    for (int i = 0; i < n; ++i) {
        const float t = xfadePos_ < xfadeLength_
                            ? static_cast<float>(xfadePos_) / static_cast<float>(xfadeLength_)
                            : 1.0f;
        for (int c = 0; c < numChannels; ++c) ch[c][i] = old[c][i] + (ch[c][i] - old[c][i]) * t;
        if (xfadePos_ < xfadeLength_) ++xfadePos_;
    }
    if (xfadePos_ >= xfadeLength_) xfadePos_ = -1;  // the outgoing chain stays until the next swap
}

}  // namespace spacer
