#include "core/clock/Timecode.h"

namespace spacer {

int Timecode::frameCount(TimecodeRate r) {
    switch (r) {
        case TimecodeRate::Fps24:
            return 24;
        case TimecodeRate::Fps25:
            return 25;
        case TimecodeRate::Fps2997Drop:
        case TimecodeRate::Fps30:
            return 30;
    }
    return 25;
}

double Timecode::framesPerSecond(TimecodeRate r) {
    return r == TimecodeRate::Fps2997Drop ? 30000.0 / 1001.0 : static_cast<double>(frameCount(r));
}

double Timecode::toSeconds() const {
    const long long fps = frameCount(rate);
    const long long totalMinutes = 60LL * hours + minutes;
    long long frameNumber = (totalMinutes * 60LL + seconds) * fps + frames;
    if (rate == TimecodeRate::Fps2997Drop) {
        // Frame numbers 0 and 1 are skipped at the start of every minute
        // except every tenth.
        frameNumber -= 2LL * (totalMinutes - totalMinutes / 10);
    }
    return static_cast<double>(frameNumber) / framesPerSecond(rate);
}

}  // namespace spacer
