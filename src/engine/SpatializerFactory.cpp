#include "engine/SpatializerFactory.h"

#include "core/spatial/AmbisonicDecoder.h"
#include "core/spatial/AmbisonicEncoder.h"
#include "core/spatial/Dbap.h"
#include "core/spatial/Direct.h"
#include "core/spatial/Vbap.h"

namespace spacer {

std::unique_ptr<Spatializer> makeSpatializer(const SourceSpec& spec, const AmbisonicDecoder* decoder) {
    switch (spec.spatializer) {
        case SpatializerKind::Direct:
            return std::make_unique<Direct>(spec.directSpeakers);
        case SpatializerKind::Vbap:
            return std::make_unique<Vbap>();
        case SpatializerKind::Dbap:
            return std::make_unique<Dbap>();  // default rolloff 6 dB (SPEC §6.2)
        case SpatializerKind::Ambisonic: {
            if (decoder == nullptr || !decoder->isPrepared()) return std::make_unique<AmbisonicEncoder>();
            auto encoder = std::make_unique<AmbisonicEncoder>(decoder->order());
            encoder->useDecoder(*decoder);
            return encoder;
        }
    }
    return std::make_unique<Vbap>();
}

}  // namespace spacer
