#pragma once

#include <span>
#include <string_view>

#include "core/Layout.h"
#include "core/Position.h"

namespace spacer {

/// Where spatializers write their output for one block. Owned by the engine.
/// `speakers[i]` is the buffer of layout speaker index i (not speaker id).
/// `ambisonic` is non-null only when an ambisonic decoder is active; its
/// channel count is (ambisonicOrder+1)^2, ACN ordering, SN3D normalisation.
struct SpeakerBus {
    float* const* speakers{nullptr};
    int numSpeakers{0};
    float* const* ambisonic{nullptr};
    int ambisonicOrder{0};
    int numSamples{0};
};

/// Source → speaker rendering. One instance per source. See SPEC §6.
/// render() is called on the audio thread and must be real-time safe; all
/// allocation happens in prepare().
class Spatializer {
public:
    virtual ~Spatializer() = default;

    virtual std::string_view name() const = 0;

    /// Non-RT. Called whenever the layout, sample rate or block size changes.
    virtual void prepare(const Layout& layout, double sampleRate, int maxBlockSize) = 0;

    /// RT. Mix `numSamples` of `mono` into the bus, moving linearly from
    /// `start` to `end` over the block (gains interpolated per sample).
    /// `spread` in [0,1]: 0 = point source, 1 = maximally wide.
    virtual void render(const float* mono, int numSamples, const Position& start, const Position& end,
                        float spread, SpeakerBus& bus) = 0;

    /// Non-RT, for tests and UI: the static gain per layout speaker for a
    /// position (empty for ambisonics, which has no per-speaker gains).
    virtual void gainsFor(const Position& pos, float spread, std::span<float> outGains) const = 0;
};

}  // namespace spacer
