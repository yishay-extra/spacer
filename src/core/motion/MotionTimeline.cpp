#include "core/motion/MotionTimeline.h"

namespace spacer {

MotionTime MotionTimeline::advance(const MotionContext& ctx) {
    const double blockSeconds = (ctx.sampleRate > 0.0 && ctx.numSamples > 0)
                                    ? static_cast<double>(ctx.numSamples) / ctx.sampleRate
                                    : 0.0;
    // Same rule as MotionContext::blockBeats(), guarded against bpm <= 0.
    const double blockBeats =
        (ctx.clock.playing && ctx.clock.bpm > 0.0) ? blockSeconds * ctx.clock.bpm / 60.0 : 0.0;
    const double endBeat = ctx.clock.beat + blockBeats;
    const double endSeconds = ctx.clock.timeSeconds + blockSeconds;

    if (pending_) {
        originBeat_ = prevBeat_ = ctx.clock.beat;
        originSeconds_ = prevSeconds_ = ctx.clock.timeSeconds;
        pending_ = false;
    }

    const MotionTime t{endBeat - originBeat_, endSeconds - originSeconds_, endBeat - prevBeat_,
                       endSeconds - prevSeconds_};
    prevBeat_ = endBeat;
    prevSeconds_ = endSeconds;
    return t;
}

}  // namespace spacer
