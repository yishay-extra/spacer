#pragma once

#include "core/Source.h"
#include "engine/SourceInput.h"

namespace spacer {

/// Device input channels (SPEC §8 `HardwareInput`): channel indices of the
/// engine's device (or aggregate). A channel the device does not have reads
/// as silence. A mono source with a stereo selection gets (L + R) / 2; a
/// stereo source with a mono selection gets the channel on both sides.
class HardwareInput final : public SourceInput {
public:
    explicit HardwareInput(const ChannelInput& channels) : channels_(channels) {}

    void prepare(double /*sampleRate*/, int /*maxBlockSize*/) override {}
    void read(const InputContext& ctx, float* const* dest, int numChannels, int numSamples) noexcept override;

private:
    ChannelInput channels_;
};

}  // namespace spacer
