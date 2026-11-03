#pragma once

// Synthetic AudioFormatReaders for the file-streaming tests.

#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>
#include <cmath>
#include <cstdint>

namespace spacer::testing {

/// Base: a reader of `length` frames whose sample values come from value().
class SyntheticReader : public juce::AudioFormatReader {
public:
    SyntheticReader(double rate, std::int64_t length, int channels = 1)
        : juce::AudioFormatReader(nullptr, "test") {
        sampleRate = rate;
        lengthInSamples = length;
        numChannels = static_cast<unsigned int>(channels);
        bitsPerSample = 32;
        usesFloatingPointData = true;
    }

    bool readSamples(int* const* dest, int numDestChannels, int startOffsetInDestBuffer,
                     juce::int64 startSampleInFile, int numSamples) override {
        onRead();
        for (int c = 0; c < numDestChannels; ++c) {
            if (dest[c] == nullptr) continue;
            auto* out = reinterpret_cast<float*>(dest[c]) + startOffsetInDestBuffer;
            for (int i = 0; i < numSamples; ++i) {
                const std::int64_t frame = startSampleInFile + i;
                out[i] = (frame >= 0 && frame < lengthInSamples) ? value(c, frame) : 0.0f;
            }
        }
        return true;
    }

    virtual float value(int channel, std::int64_t frame) const = 0;
    virtual void onRead() {}
};

/// Sample value = (frame + 1) · 1e-6 (channel 1: negative), so every sample
/// names its frame exactly (float holds integers · 1e-6 well enough below
/// 2^23 · 1e-6).
class RampReader : public SyntheticReader {
public:
    using SyntheticReader::SyntheticReader;
    static float valueOf(std::int64_t frame) { return static_cast<float>(frame + 1) * 1e-6f; }
    static std::int64_t frameOf(float v) { return std::llround(v * 1e6) - 1; }
    float value(int channel, std::int64_t frame) const override {
        return channel == 0 ? valueOf(frame) : -valueOf(frame);
    }
};

class SineReader : public SyntheticReader {
public:
    SineReader(double rate, double hz, double amplitude, std::int64_t length)
        : SyntheticReader(rate, length), hz_(hz), amplitude_(amplitude) {}
    float value(int, std::int64_t frame) const override {
        return static_cast<float>(amplitude_ * std::sin(2.0 * juce::MathConstants<double>::pi * hz_ *
                                                        static_cast<double>(frame) / sampleRate));
    }

private:
    double hz_, amplitude_;
};

/// A ramp reader that blocks inside readSamples() while `stalled` is set.
class StallingReader : public RampReader {
public:
    using RampReader::RampReader;
    std::atomic<bool> stalled{false};
    std::atomic<int> readsInProgress{0};
    void onRead() override {
        ++readsInProgress;
        while (stalled.load()) juce::Thread::sleep(1);
        --readsInProgress;
    }
};

}  // namespace spacer::testing
