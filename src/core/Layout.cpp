#include "core/Layout.h"

#include <algorithm>
#include <cmath>

namespace spacer {

std::vector<SpeakerCompensation> computeCompensation(const Layout& layout, float speedOfSound) {
    std::vector<SpeakerCompensation> out;
    out.reserve(layout.speakers.size());
    if (layout.speakers.empty()) return out;

    float maxDistance = 0.0f;
    for (const auto& s : layout.speakers) maxDistance = std::max(maxDistance, s.position.distance);

    for (const auto& s : layout.speakers) {
        SpeakerCompensation c;
        c.speakerId = s.id;
        // Nearer speakers are delayed so they arrive with the farthest one.
        c.delayMs = (maxDistance - s.position.distance) / speedOfSound * 1000.0f;
        // 1/r: nearer speakers are attenuated to match the farthest one.
        // Guard against a speaker at the origin.
        const float d = std::max(s.position.distance, 0.01f);
        c.gainDb = maxDistance > 0.0f ? 20.0f * std::log10(d / std::max(maxDistance, 0.01f)) : 0.0f;
        out.push_back(c);
    }
    return out;
}

}  // namespace spacer
