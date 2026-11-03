#include "engine/SourceInputFactory.h"

#include <variant>

#include "engine/FilePlayer.h"
#include "engine/GeneratorInput.h"
#include "engine/HardwareInput.h"

namespace spacer {

SourceInputFactory::SourceInputFactory() {
    // WAV, AIFF, FLAC, Ogg Vorbis; on macOS also CoreAudio (MP3, AAC, ALAC).
    formats_.registerBasicFormats();
}

juce::File SourceInputFactory::resolve(const FileInputSpec& spec) const {
    const juce::String path(juce::CharPointer_UTF8(spec.path.c_str()));
    const bool absolute = juce::File::isAbsolutePath(path);
    const juce::File asGiven =
        absolute ? juce::File(path) : juce::File::getCurrentWorkingDirectory().getChildFile(path);
    if (projectDirectory_ == juce::File()) return asGiven;
    const juce::File inProject = absolute ? projectDirectory_.getChildFile(asGiven.getFileName())
                                          : projectDirectory_.getChildFile(path);
    const bool preferProject = spec.relativeToProject || !absolute;
    const juce::File& first = preferProject ? inProject : asGiven;
    const juce::File& second = preferProject ? asGiven : inProject;
    return first.existsAsFile() || !second.existsAsFile() ? first : second;
}

std::unique_ptr<SourceInput> SourceInputFactory::create(const SourceSpec& spec,
                                                        std::vector<std::string>* warnings) {
    const int channels = spec.stereo ? 2 : 1;
    return std::visit(
        [&](const auto& in) -> std::unique_ptr<SourceInput> {
            using T = std::decay_t<decltype(in)>;
            if constexpr (std::is_same_v<T, HardwareInputSpec>) {
                return std::make_unique<HardwareInput>(in);
            } else if constexpr (std::is_same_v<T, GeneratorSpec>) {
                return std::make_unique<GeneratorInput>(in);
            } else if constexpr (std::is_same_v<T, FileInputSpec>) {
                const juce::File file = resolve(in);
                std::unique_ptr<juce::AudioFormatReader> reader;
                if (file.existsAsFile()) reader.reset(formats_.createReaderFor(file));
                if (!reader && warnings)
                    warnings->push_back("source " + std::to_string(spec.id) + ": cannot read audio file '" +
                                        file.getFullPathName().toStdString() + "'");
                return std::make_unique<FilePlayer>(in, channels, std::move(reader), streamer_,
                                                    file.getFullPathName().toStdString());
            } else {
                return nullptr;  // VirtualInput: WP5
            }
        },
        spec.input.value);
}

}  // namespace spacer
