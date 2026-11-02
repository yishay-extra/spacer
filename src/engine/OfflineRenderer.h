#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "core/Expected.h"
#include "core/Project.h"

namespace spacer {

/// Renders a project without a device (SPEC §5.7), through the same
/// EngineProcessor / Graph as the live engine: the output channel layout is
/// the device's (speaker i → channel `outputChannel`; unassigned speakers are
/// not rendered). Used for golden tests and, later, "bounce layout test".
///
/// `inputs` override sources by id (audio already at the render rate); every
/// other source uses its own input: generators and files render (files are
/// started at t = 0 and streamed through the same FilePlayer / disk path as
/// live, serviced synchronously before every block so the result does not
/// depend on disk timing); hardware and virtual inputs are silent. The clock
/// is configured from the project and running.
struct OfflineRenderOptions {
    double sampleRate{0.0};        // 0 = project.devices.sampleRate
    int blockSize{0};              // 0 = project.devices.bufferSize
    std::string projectDirectory;  // base for project-relative file paths
};

struct OfflineRenderResult {
    juce::AudioBuffer<float> audio;
    double sampleRate{0.0};
    int latencySamples{0};              // graph latency (limiter look-ahead) included in `audio`
    std::vector<std::string> warnings;  // e.g. unreadable files (rendered silent)
};

class OfflineRenderer {
public:
    using Options = OfflineRenderOptions;
    using Result = OfflineRenderResult;
    using Inputs = std::map<int, juce::AudioBuffer<float>>;

    static Expected<Result, std::string> render(const Project& project, const Inputs& inputs, double seconds,
                                                const Options& options = Options{});

    /// Sources fed from audio files (any format the engine reads), played
    /// from the start through the file player (resampled to the render rate).
    /// An unreadable file is an error here.
    static Expected<Result, std::string> renderFiles(const Project& project,
                                                     const std::map<int, juce::File>& files, double seconds,
                                                     const Options& options = Options{});

    /// Writes a 32-bit float WAV. Returns an error message on failure.
    static std::optional<std::string> writeWav(const Result& result, const juce::File& file);
};

}  // namespace spacer
