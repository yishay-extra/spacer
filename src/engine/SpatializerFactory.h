#pragma once

#include <memory>

#include "core/Source.h"
#include "core/Spatializer.h"

namespace spacer {

/// Non-RT. The spatializer instance for a source (SPEC §6), not yet prepared.
///
/// Until the ambisonic encoder lands (WP2d), `Ambisonic` sources fall back to
/// VBAP so they still render; `isFallback` reports it.
std::unique_ptr<Spatializer> makeSpatializer(const SourceSpec& spec, bool* isFallback = nullptr);

}  // namespace spacer
