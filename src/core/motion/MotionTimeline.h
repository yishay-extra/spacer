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

private:
    bool pending_{true};
    double originBeat_{0.0};
    double originSeconds_{0.0};
    double prevBeat_{0.0};
    double prevSeconds_{0.0};
};

}  // namespace spacer
