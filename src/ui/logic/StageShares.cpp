#include "ui/logic/StageShares.h"

#include <algorithm>

#include "core/spatial/Direct.h"

namespace spacer {

namespace {
constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 64;
}  // namespace

void StageShares::prepare(const Layout& layout) {
    layout_ = layout;
    vbap_.prepare(layout_, kSampleRate, kBlockSize);
    dbap_.prepare(layout_, kSampleRate, kBlockSize);
    numSpeakers_ = static_cast<int>(layout_.speakers.size());
}

bool StageShares::gainsFor(const SourceSpec& source, const Position& position, std::span<float> out) const {
    std::fill(out.begin(), out.end(), 0.0f);
    if (numSpeakers_ == 0 || out.size() != static_cast<std::size_t>(numSpeakers_)) return false;
    switch (source.spatializer) {
        case SpatializerKind::Vbap:
            vbap_.gainsFor(position, source.spread, out);
            return true;
        case SpatializerKind::Dbap:
            dbap_.gainsFor(position, source.spread, out);
            return true;
        case SpatializerKind::Direct: {
            Direct direct(source.directSpeakers);
            direct.prepare(layout_, kSampleRate, kBlockSize);
            direct.gainsFor(position, source.spread, out);
            return true;
        }
        case SpatializerKind::Ambisonic:
            return false;
    }
    return false;
}

}  // namespace spacer
