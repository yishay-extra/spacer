#pragma once

// Per-speaker gains for one source point, computed by the engine's own
// panners (Vbap, Dbap, Direct) so the stage shows what is actually heard.

#include <span>

#include "core/Layout.h"
#include "core/Position.h"
#include "core/Source.h"
#include "core/spatial/Dbap.h"
#include "core/spatial/Vbap.h"

namespace spacer {

class StageShares {
public:
    /// Non-RT. Builds the panners for `layout`; call again when the layout changes.
    void prepare(const Layout& layout);
    int numSpeakers() const { return numSpeakers_; }

    /// Linear gain per layout speaker index for one point of `source` at `position`, using
    /// `source.spatializer`, `source.spread` and `source.directSpeakers`. Returns false and
    /// zero-fills `out` when there are no per-speaker gains: Ambisonic source, not prepared,
    /// or `out.size() != numSpeakers()`.
    bool gainsFor(const SourceSpec& source, const Position& position, std::span<float> out) const;

private:
    Layout layout_;
    Vbap vbap_;
    Dbap dbap_;
    int numSpeakers_{0};
};

}  // namespace spacer
