#pragma once

#include "core/Source.h"
#include "engine/HardwareInput.h"
#include "engine/SourceInput.h"

namespace spacer {

/// Spacer.driver channels (SPEC §8 `VirtualInput`, §14): the same channel
/// selection rules as HardwareInput (mono / stereo, missing channel =
/// silence), applied to InputContext::virtualInputs, which the engine fills
/// once per block from the VirtualDeviceReceiver. Without the driver (or in
/// an offline render) the context has no virtual channels and the source is
/// silent.
class VirtualInput final : public SourceInput {
public:
    explicit VirtualInput(const ChannelInput& channels) : select_(channels) {}

    void prepare(double sampleRate, int maxBlockSize) override { select_.prepare(sampleRate, maxBlockSize); }
    void read(const InputContext& ctx, float* const* dest, int numChannels,
              int numSamples) noexcept override {
        InputContext view;
        view.deviceInputs = ctx.virtualInputs;
        view.numDeviceInputs = ctx.virtualInputs != nullptr ? ctx.numVirtualInputs : 0;
        select_.read(view, dest, numChannels, numSamples);
    }

private:
    HardwareInput select_;
};

}  // namespace spacer
