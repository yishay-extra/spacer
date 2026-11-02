#include "engine/BufferInput.h"

#include <algorithm>
#include <utility>

namespace spacer {

BufferInput::BufferInput(juce::AudioBuffer<float> audio) : audio_(std::move(audio)) {}

void BufferInput::prepare(double /*sampleRate*/, int /*maxBlockSize*/) {
    position_ = 0;
}

void BufferInput::read(const InputContext& /*ctx*/, float* const* dest, int numChannels, int n) noexcept {
    const int available = std::clamp(audio_.getNumSamples() - position_, 0, n);
    const int srcChannels = audio_.getNumChannels();
    for (int c = 0; c < numChannels; ++c) {
        float* out = dest[c];
        if (srcChannels == 0 || available == 0) {
            std::fill(out, out + n, 0.0f);
            continue;
        }
        if (numChannels == 1 && srcChannels > 1) {
            std::fill(out, out + available, 0.0f);
            const float scale = 1.0f / static_cast<float>(srcChannels);
            for (int s = 0; s < srcChannels; ++s) {
                const float* in = audio_.getReadPointer(s, position_);
                for (int i = 0; i < available; ++i) out[i] += in[i] * scale;
            }
        } else {
            const float* in = audio_.getReadPointer(std::min(c, srcChannels - 1), position_);
            std::copy(in, in + available, out);
        }
        std::fill(out + available, out + n, 0.0f);
    }
    position_ += available;
}

void BufferInput::takeStateFrom(SourceInput& old) noexcept {
    if (auto* o = dynamic_cast<BufferInput*>(&old)) position_ = o->position_;
}

}  // namespace spacer
