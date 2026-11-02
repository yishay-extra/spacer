#include "engine/OfflineRenderer.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>
#include <vector>

#include "engine/BufferInput.h"
#include "engine/EngineProcessor.h"
#include "engine/GraphBuilder.h"

namespace spacer {

Expected<OfflineRenderer::Result, std::string> OfflineRenderer::render(const Project& project,
                                                                       const Inputs& inputs, double seconds,
                                                                       const Options& options) {
    const double sampleRate = options.sampleRate > 0.0 ? options.sampleRate : project.devices.sampleRate;
    const int blockSize = options.blockSize > 0 ? options.blockSize : project.devices.bufferSize;
    if (!(sampleRate > 0.0) || blockSize <= 0)
        return Unexpected(std::string("invalid sample rate or block size"));
    if (!(seconds >= 0.0)) return Unexpected(std::string("invalid duration"));
    const int numOutputs = GraphBuilder::requiredOutputChannels(project.layout);
    if (numOutputs <= 0) return Unexpected(std::string("no speaker has an output channel"));

    GraphBuilder builder(project);
    builder.setInputFactory([&inputs](const SourceSpec& spec) -> std::unique_ptr<SourceInput> {
        const auto it = inputs.find(spec.id);
        if (it == inputs.end()) return nullptr;
        return std::make_unique<BufferInput>(it->second);
    });

    auto processor = std::make_unique<EngineProcessor>();
    processor->configureClock(project.clock);
    processor->setGraphWhileStopped(builder.build(sampleRate, blockSize));
    processor->push(cmd::ClockContinue{});

    Result result;
    result.sampleRate = sampleRate;
    result.latencySamples = processor->latencySamples();
    const auto total = static_cast<int>(std::llround(seconds * sampleRate));
    result.audio.setSize(numOutputs, total);
    result.audio.clear();

    std::vector<float*> out(static_cast<std::size_t>(numOutputs));
    Feedback discard;
    for (int pos = 0; pos < total; pos += blockSize) {
        const int n = std::min(blockSize, total - pos);
        for (int c = 0; c < numOutputs; ++c)
            out[static_cast<std::size_t>(c)] = result.audio.getWritePointer(c, pos);
        processor->process(nullptr, 0, out.data(), numOutputs, n);
        while (processor->popFeedback(discard)) {
        }
        processor->collectGarbage();
    }
    return result;
}

Expected<OfflineRenderer::Result, std::string> OfflineRenderer::renderFiles(
    const Project& project, const std::map<int, juce::File>& files, double seconds, const Options& options) {
    const double sampleRate = options.sampleRate > 0.0 ? options.sampleRate : project.devices.sampleRate;
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    Inputs inputs;
    for (const auto& [id, file] : files) {
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
        if (!reader) return Unexpected("cannot read " + file.getFullPathName().toStdString());
        if (std::abs(reader->sampleRate - sampleRate) > 0.5)
            return Unexpected(file.getFileName().toStdString() + ": sample rate " +
                              std::to_string(reader->sampleRate) +
                              " does not match the render rate (resampling is WP4)");
        juce::AudioBuffer<float> audio(static_cast<int>(reader->numChannels),
                                       static_cast<int>(reader->lengthInSamples));
        reader->read(&audio, 0, audio.getNumSamples(), 0, true, true);
        inputs.emplace(id, std::move(audio));
    }
    return render(project, inputs, seconds, options);
}

std::optional<std::string> OfflineRenderer::writeWav(const Result& result, const juce::File& file) {
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream>(file);
    if (static_cast<juce::FileOutputStream*>(stream.get())->failedToOpen())
        return "cannot open " + file.getFullPathName().toStdString();
    juce::WavAudioFormat wav;
    const auto opts = juce::AudioFormatWriterOptions{}
                          .withSampleRate(result.sampleRate)
                          .withNumChannels(result.audio.getNumChannels())
                          .withBitsPerSample(32)
                          .withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
    auto writer = wav.createWriterFor(stream, opts);
    if (!writer) return std::string("cannot create WAV writer");
    if (!writer->writeFromAudioSampleBuffer(result.audio, 0, result.audio.getNumSamples()))
        return std::string("write failed");
    return std::nullopt;
}

}  // namespace spacer
