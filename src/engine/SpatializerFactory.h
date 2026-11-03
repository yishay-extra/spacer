#pragma once

#include <memory>

#include "core/Source.h"
#include "core/Spatializer.h"

namespace spacer {

class AmbisonicDecoder;

/// Non-RT. The spatializer instance for a source (SPEC §6), not yet prepared.
///
/// `Ambisonic` sources get an `AmbisonicEncoder` of the project decoder's
/// order, whose spread compensation uses that decoder (no per-source decoder
/// build). Without a decoder the encoder builds its own in prepare() at order 3.
std::unique_ptr<Spatializer> makeSpatializer(const SourceSpec& spec,
                                             const AmbisonicDecoder* decoder = nullptr);

}  // namespace spacer
