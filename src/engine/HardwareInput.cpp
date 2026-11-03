#include "engine/HardwareInput.h"

#include <algorithm>

namespace spacer {

namespace {
const float* channelOf(const InputContext& ctx, int index) noexcept {
    if (index < 0 || index >= ctx.numDeviceInputs || ctx.deviceInputs == nullptr) return nullptr;
    return ctx.deviceInputs[index];
}

void copyOrClear(const float* in, float* out, int n) noexcept {
    if (in != nullptr)
        std::copy(in, in + n, out);
    else
        std::fill(out, out + n, 0.0f);
}
}  // namespace

void HardwareInput::read(const InputContext& ctx, float* const* dest, int numChannels, int n) noexcept {
    if (numChannels <= 0) return;
    const float* left = channelOf(ctx, channels_.left);
    const float* right = channels_.isStereo() ? channelOf(ctx, channels_.right) : nullptr;

    if (numChannels == 1) {
        if (!channels_.isStereo()) {
            copyOrClear(left, dest[0], n);
            return;
        }
        float* out = dest[0];
        std::fill(out, out + n, 0.0f);
        for (const float* in : {left, right})
            if (in != nullptr)
                for (int i = 0; i < n; ++i) out[i] += 0.5f * in[i];
        return;
    }
    copyOrClear(left, dest[0], n);
    copyOrClear(channels_.isStereo() ? right : left, dest[1], n);
}

}  // namespace spacer
