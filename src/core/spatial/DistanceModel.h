#pragma once

#include "core/Source.h"

namespace spacer {

/// SPEC §6.5: g = (r_ref / max(d, r_min))^k, r_ref = 1 m. k = 0 gives unity.
float distanceGain(const DistanceModel& model, float distance);

/// SPEC §6.5: fc = 20 kHz · (r_ref / max(d, r_ref))^q. q = 0 means "off" and
/// returns 20 kHz (callers should bypass the filter instead; see
/// DistanceProcessor).
float distanceCutoffHz(const DistanceModel& model, float distance);

/// Per-source (per-channel) distance gain + optional one-pole low-pass,
/// applied to the mono signal before spatialising. The gain and the filter
/// coefficient move linearly across the block from the start to the end
/// distance. No allocation; safe on the audio thread.
class DistanceProcessor {
public:
    void prepare(double sampleRate);
    void reset();

    /// In-place. `lastDistance` of the previous block should equal
    /// `startDistance` for glitch-free output (the caller's concern).
    void process(float* samples, int numSamples, float startDistance, float endDistance,
                 const DistanceModel& model);

private:
    double sampleRate_{48000.0};
    float state_{0.0f};
};

}  // namespace spacer
