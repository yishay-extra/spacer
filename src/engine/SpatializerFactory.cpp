#include "engine/SpatializerFactory.h"

#include "core/spatial/Dbap.h"
#include "core/spatial/Direct.h"
#include "core/spatial/Vbap.h"

namespace spacer {

std::unique_ptr<Spatializer> makeSpatializer(const SourceSpec& spec, bool* isFallback) {
    if (isFallback) *isFallback = false;
    switch (spec.spatializer) {
        case SpatializerKind::Direct:
            return std::make_unique<Direct>(spec.directSpeakers);
        case SpatializerKind::Vbap:
            return std::make_unique<Vbap>();
        case SpatializerKind::Dbap:
            return std::make_unique<Dbap>();  // default rolloff 6 dB (SPEC §6.2)
        case SpatializerKind::Ambisonic:      // WP2d
            break;
    }
    if (isFallback) *isFallback = true;
    return std::make_unique<Vbap>();
}

}  // namespace spacer
