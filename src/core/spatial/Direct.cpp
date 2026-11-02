#include "core/spatial/Direct.h"

#include <algorithm>
#include <utility>

namespace spacer {

Direct::Direct(std::vector<int> speakerIds) : speakerIds_(std::move(speakerIds)) {}

void Direct::prepare(const Layout& layout, double /*sampleRate*/, int /*maxBlockSize*/) {
    numTargets_ = 0;
    for (int id : speakerIds_) {
        for (std::size_t i = 0; i < layout.speakers.size() && i < targets_.size(); ++i) {
            if (layout.speakers[i].id != id) continue;
            const int index = static_cast<int>(i);
            const auto end = targets_.begin() + numTargets_;
            if (std::find(targets_.begin(), end, index) == end && numTargets_ < kMaxSpeakers)
                targets_[static_cast<std::size_t>(numTargets_++)] = index;
            break;
        }
    }
}

void Direct::render(const float* mono, int numSamples, const Position& /*start*/, const Position& /*end*/,
                    float /*spread*/, SpeakerBus& bus) {
    for (int t = 0; t < numTargets_; ++t) {
        const int s = targets_[static_cast<std::size_t>(t)];
        if (s >= bus.numSpeakers) continue;
        float* out = bus.speakers[s];
        for (int i = 0; i < numSamples; ++i) out[i] += mono[i];
    }
}

void Direct::gainsFor(const Position& /*pos*/, float /*spread*/, std::span<float> outGains) const {
    std::fill(outGains.begin(), outGains.end(), 0.0f);
    for (int t = 0; t < numTargets_; ++t) {
        const auto s = static_cast<std::size_t>(targets_[static_cast<std::size_t>(t)]);
        if (s < outGains.size()) outGains[s] = 1.0f;
    }
}

}  // namespace spacer
