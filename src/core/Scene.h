#pragma once

// Scene = snapshot of all mutable state except layout/device (SPEC §11).

#include <string>
#include <vector>

#include "core/Source.h"
#include "core/SpeakerChain.h"

namespace spacer {

struct Scene {
    std::string name;
    std::vector<SourceSpec> sources;
    std::vector<SpeakerChainSpec> speakerChains;
    MasterSpec master;
    bool operator==(const Scene&) const = default;
};

}  // namespace spacer
