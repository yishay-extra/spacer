#pragma once

// Helpers shared by the engine tests.

#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>
#include <string>
#include <vector>

#include "core/Project.h"

namespace spacer::testing {

inline Speaker makeSpeaker(int id, float az, float dist, int channel, bool lfe = false) {
    Speaker s;
    s.id = id;
    s.name = std::to_string(id);
    s.position = {az, 0.0f, dist};
    s.outputChannel = channel;
    s.lfe = lfe;
    return s;
}

inline SourceSpec makeSource(int id, Position pos, SpatializerKind kind = SpatializerKind::Vbap) {
    SourceSpec s;
    s.id = id;
    s.name = "src" + std::to_string(id);
    s.input.value = GeneratorSpec{};
    s.spatializer = kind;
    s.distance.k = 0.0f;  // no distance attenuation unless a test asks for it
    s.motion.value = ManualMotion{pos, 0.0f};
    return s;
}

inline Project makeProject(std::vector<Speaker> speakers, std::vector<SourceSpec> sources = {}) {
    Project p;
    p.layout.name = "test";
    p.layout.speakers = std::move(speakers);
    p.sources = std::move(sources);
    p.devices.sampleRate = 48000.0;
    p.devices.bufferSize = 128;
    return p;
}

inline juce::AudioBuffer<float> sine(double sampleRate, double hz, float amplitude, int numSamples) {
    juce::AudioBuffer<float> b(1, numSamples);
    for (int i = 0; i < numSamples; ++i)
        b.setSample(0, i,
                    amplitude * static_cast<float>(
                                    std::sin(2.0 * juce::MathConstants<double>::pi * hz * i / sampleRate)));
    return b;
}

inline float rms(const float* x, int n) {
    double s = 0.0;
    for (int i = 0; i < n; ++i) s += static_cast<double>(x[i]) * x[i];
    return n > 0 ? static_cast<float>(std::sqrt(s / n)) : 0.0f;
}

inline float peakAbs(const float* x, int n) {
    float p = 0.0f;
    for (int i = 0; i < n; ++i) p = std::max(p, std::abs(x[i]));
    return p;
}

/// Phase (radians) of a sinusoid of frequency hz in x[start, start+n), by
/// projection onto sin / cos. n should span whole periods.
inline double phaseOf(const float* x, int start, int n, double hz, double sampleRate) {
    double s = 0.0, c = 0.0;
    for (int i = start; i < start + n; ++i) {
        const double w = 2.0 * juce::MathConstants<double>::pi * hz * i / sampleRate;
        s += x[i] * std::sin(w);
        c += x[i] * std::cos(w);
    }
    return std::atan2(c, s);
}

}  // namespace spacer::testing
