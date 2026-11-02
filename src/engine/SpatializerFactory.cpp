#include "engine/SpatializerFactory.h"

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
        case SpatializerKind::Dbap:       // WP2c
        case SpatializerKind::Ambisonic:  // WP2d
            break;
    }
    if (isFallback) *isFallback = true;
    return std::make_unique<Vbap>();
}

}  // namespace spacer
