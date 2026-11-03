#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <cstdint>
#include <memory>
#include <string>

#include "core/Source.h"
#include "engine/FileStream.h"
#include "engine/SourceInput.h"

namespace spacer {

class DiskStreamer;

/// The audio-thread side of a file source (SPEC §8 `File`): plays the
/// FileStream's ring with transport play / stop / seek and loop, applying
/// the file's `gainDb`. Starts stopped at `startSeconds` (prefilled, so play
/// is immediate).
///
/// Timing: play / stop take effect at the start of the block they arrive in.
/// A seek discards the buffered audio; playback resumes, exactly at the
/// requested frame, as soon as the disk side has refilled (the next block in
/// the offline renderer; a few ms live). Until then the source is silent.
/// Underruns (the disk side falling behind) play silence and are counted;
/// the stream continues where it was, so no audio is skipped.
///
/// prepare() (non-RT) creates and primes the stream and registers it with
/// the streamer; null streamer = nothing refills the ring after priming.
class FilePlayer final : public SourceInput {
public:
    /// `reader` may be null (missing file): the player is then silent.
    /// `identity` names the file for graph-swap carry-over (resolved path).
    FilePlayer(const FileInputSpec& spec, int numChannels, std::unique_ptr<juce::AudioFormatReader> reader,
               DiskStreamer* streamer, std::string identity, bool forceInterpolation = false);
    ~FilePlayer() override;

    void prepare(double sampleRate, int maxBlockSize) override;
    void read(const InputContext& ctx, float* const* dest, int numChannels, int numSamples) noexcept override;
    /// Same file at the same rate: takes over the running stream (no gap,
    /// buffered audio kept; loop setting updated). Same file at another rate:
    /// continues from the old position and transport state.
    void takeStateFrom(SourceInput& old) noexcept override;

    void play() noexcept override;
    void stop() noexcept override;
    void seek(double seconds) noexcept override;
    bool transportState(TransportState& out) const noexcept override;

    bool playing() const noexcept { return playing_; }
    bool atEnd() const noexcept { return ended_; }
    /// File time of the next sample to be played.
    double positionSeconds() const noexcept;
    std::uint32_t underruns() const noexcept { return underruns_; }
    const FileStream* stream() const noexcept { return stream_.get(); }

private:
    void seekFrame(std::int64_t frame) noexcept;

    FileInputSpec spec_;
    int channels_;
    std::unique_ptr<juce::AudioFormatReader> reader_;  // until prepare()
    DiskStreamer* streamer_;
    std::string identity_;
    bool forceInterpolation_;
    float gain_{1.0f};
    std::shared_ptr<FileStream> stream_;

    // Consumer state (audio thread).
    std::int64_t generation_{0};
    bool waiting_{true};  // for the producer to accept `generation_`
    bool playing_{false};
    bool ended_{false};
    std::int64_t baseFrame_{0};  // file frame where the current generation starts
    std::int64_t consumed_{0};   // output frames played since then
    std::uint32_t underruns_{0};
};

}  // namespace spacer
