#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <memory>
#include <string>
#include <vector>

#include "core/Source.h"
#include "engine/SourceInput.h"

namespace spacer {

class DiskStreamer;

/// Creates the live input for a source from its InputSpec (SPEC §8). Non-RT.
///   - HardwareInput → HardwareInput (device channels);
///   - File          → FilePlayer streamed by `streamer`;
///   - Generator     → GeneratorInput;
///   - VirtualInput  → null (silence) until WP5 provides the driver reader.
/// Problems (missing / unreadable file) are appended to `warnings` and give a
/// silent input, never an error: a project with a missing file must still
/// play everything else.
class SourceInputFactory {
public:
    SourceInputFactory();

    void setDiskStreamer(DiskStreamer* streamer) { streamer_ = streamer; }
    /// Base for paths relative to the project file.
    void setProjectDirectory(const juce::File& dir) { projectDirectory_ = dir; }

    std::unique_ptr<SourceInput> create(const SourceSpec& spec, std::vector<std::string>* warnings = nullptr);

    /// Where a file source's audio is looked for. `relativeTo: "project"`
    /// (or any relative path) resolves against the project directory; if
    /// that does not exist the path is tried as given (SPEC §15 "paths as
    /// given plus relativeTo project fallback"), and vice versa.
    juce::File resolve(const FileInputSpec& spec) const;

    juce::AudioFormatManager& formats() noexcept { return formats_; }

private:
    DiskStreamer* streamer_{nullptr};
    juce::File projectDirectory_;
    juce::AudioFormatManager formats_;
};

}  // namespace spacer
