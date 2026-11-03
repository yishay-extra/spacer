#pragma once

#include "core/Motion.h"
#include "core/motion/MotionTime.h"

namespace spacer {

/// Turns successive MotionContexts into MotionTime relative to an origin.
/// After restart() the origin is taken from the START of the next block
/// passed to advance(), so phase 0 is the first sample of that block.
/// Real-time safe.
class MotionTimeline {
public:
    void restart() { pending_ = true; }

    MotionTime advance(const MotionContext& ctx);

    /// Live period change (SPEC §8: re-anchor the phase so the position is
    /// continuous): moves the origin so that the number of `to` periods
    /// elapsed at the end of the last block equals the number of `from`
    /// periods elapsed then. Units may differ. No effect before the first
    /// advance() or for non-positive periods. RT-safe.
    void reanchor(const Period& from, const Period& to);

private:
    bool pending_{true};
    double originBeat_{0.0};
    double originSeconds_{0.0};
    double prevBeat_{0.0};
    double prevSeconds_{0.0};
};

}  // namespace spacer
