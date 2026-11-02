#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <map>
#include <optional>
#include <string>

#include "core/Expected.h"
#include "core/Project.h"

namespace spacer {

/// Renders a project without a device (SPEC §5.7), through the same
/// EngineProcessor / Graph as the live engine: the output channel layout is
/// the device's (speaker i → channel `outputChannel`; unassigned speakers are
/// not rendered). Used for golden tests and, later, "bounce layout test".
///
/// Inputs are per source id; sources without an input render silence (live
/// input kinds are WP4/WP5). Input audio must already be at the render rate.
/// The clock is configured from the project and running.
struct OfflineRenderOptions {
    double sampleRate{0.0};  // 0 = project.devices.sampleRate
    int blockSize{0};        // 0 = project.devices.bufferSize
};

struct OfflineRenderResult {
    juce::AudioBuffer<float> audio;
    double sampleRate{0.0};
    int latencySamples{0};  // graph latency (limiter look-ahead) included in `audio`
};

class OfflineRenderer {
public:
    using Options = OfflineRenderOptions;
    using Result = OfflineRenderResult;
    using Inputs = std::map<int, juce::AudioBuffer<float>>;

    static Expected<Result, std::string> render(const Project& project, const Inputs& inputs, double seconds,
                                                const Options& options = Options{});

    /// Inputs read from audio files (any format JUCE reads); each file's rate
    /// must match the render rate.
    static Expected<Result, std::string> renderFiles(const Project& project,
                                                     const std::map<int, juce::File>& files, double seconds,
                                                     const Options& options = Options{});

    /// Writes a 32-bit float WAV. Returns an error message on failure.
    static std::optional<std::string> writeWav(const Result& result, const juce::File& file);
};

}  // namespace spacer
