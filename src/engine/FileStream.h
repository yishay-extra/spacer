#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include "engine/StreamRing.h"

namespace spacer {

/// The disk side of one file player (SPEC §8, §5.1): decodes an audio file,
/// resamples it to the engine rate and keeps a StreamRing filled ahead of
/// playback. Shared (std::shared_ptr) between the FilePlayer on the audio
/// thread, which consumes the ring, and the DiskStreamer, which calls
/// service() on the disk thread.
///
/// Stream model: the producer writes the file as one continuous stream at the
/// engine rate, starting at the frame requested by the latest seek
/// "generation"; with `loop` the region [loopStart, end) repeats without a
/// gap (the resampler runs across the loop point, so looping is
/// sample-accurate in file time). Seeks are requested by the consumer
/// (requestSeek); the producer accepts a generation by publishing the ring
/// count at which its audio begins (boundary) and, without loop, the count at
/// which it ends (endCount).
///
/// Resampling (file rate ≠ engine rate): juce::LagrangeInterpolator, primed
/// with the file audio before the seek point so that output frame k is file
/// time `seekFrame + k·ratio` with no start-up transient. (JUCE's
/// WindowedSincInterpolator was measured at ~1 % passband gain error, so it is
/// not used.) When downsampling, a linear-phase FIR low-pass (juce::dsp::FIR,
/// 257 taps, Blackman-Harris) at 0.45 × engine rate runs first against
/// aliasing; its constant delay is compensated in the priming.
///
/// Threads: the constructor, prime() and the destructor are non-RT;
/// service() is for one disk-side thread at a time; requestSeek(), setLoop()
/// and the ring's consumer side are for the audio thread.
class FileStream {
public:
    static constexpr int kChunkFrames = 2048;
    static constexpr double kDefaultRingSeconds = 1.0;

    struct Options {
        int numChannels{1};          // stream channels (1 or 2), the source's
        double engineRate{48000.0};  // output rate
        bool loop{false};
        double startSeconds{0.0};        // initial position, and the loop start
        int ringFrames{0};               // 0 = kDefaultRingSeconds at the engine rate
        bool forceInterpolation{false};  // tests: resample even at ratio 1
    };

    FileStream(std::unique_ptr<juce::AudioFormatReader> reader, const Options& options);
    ~FileStream();
    FileStream(const FileStream&) = delete;
    FileStream& operator=(const FileStream&) = delete;

    // ---- info (any thread) ----
    double fileRate() const noexcept { return fileRate_; }
    double engineRate() const noexcept { return engineRate_; }
    /// File frames per output frame.
    double ratio() const noexcept { return ratio_; }
    std::int64_t lengthFrames() const noexcept { return length_; }
    int numChannels() const noexcept { return channels_; }
    std::int64_t startFrame() const noexcept { return startFrame_; }
    bool loop() const noexcept { return loop_.load(std::memory_order_relaxed); }

    // ---- producer (disk side) ----
    /// Fills the ring with up to `maxFrames` frames (or until full / file
    /// end). Handles a pending seek first. Returns the frames written. May
    /// block in the file reader; never touches audio-thread state.
    int service(int maxFrames = 1 << 30);
    /// Non-RT, before the audio thread uses the stream: fill the whole ring.
    void prime() { service(); }

    // ---- consumer (audio thread) ----
    StreamRing& ring() noexcept { return ring_; }
    /// Ask the producer to restart at `frame` (clamped to [0, length]).
    /// `generation` must increase with every request.
    void requestSeek(std::int64_t generation, std::int64_t frame) noexcept;
    std::int64_t acceptedGeneration() const noexcept { return accepted_.load(std::memory_order_acquire); }
    /// Ring count where the accepted generation's audio starts.
    std::int64_t boundary() const noexcept { return boundary_.load(std::memory_order_acquire); }
    /// Ring count where the accepted generation's audio ends (no loop), or
    /// INT64_MAX while unknown / looping.
    std::int64_t endCount() const noexcept { return endCount_.load(std::memory_order_acquire); }
    /// Takes effect from the producer's current read position.
    void setLoop(bool loop) noexcept { loop_.store(loop, std::memory_order_relaxed); }

private:
    void restartAt(std::int64_t frame);
    void readInput(int frames);
    int remainingOutput() const noexcept;
    void produce(int frames);

    std::unique_ptr<juce::AudioFormatReader> reader_;
    int channels_;
    double fileRate_;
    double engineRate_;
    double ratio_;
    std::int64_t length_;
    std::int64_t startFrame_;
    bool interpolate_;

    StreamRing ring_;

    // Consumer → producer.
    std::atomic<std::int64_t> requestGen_{0};
    std::atomic<std::int64_t> requestFrame_{0};
    std::atomic<bool> loop_;
    // Producer → consumer.
    std::atomic<std::int64_t> accepted_{-1};
    std::atomic<std::int64_t> boundary_{0};
    std::atomic<std::int64_t> endCount_{INT64_MAX};

    // Producer state.
    std::int64_t producerGen_{-1};
    std::int64_t filePos_{0};  // next file frame to read
    double outPos_{0.0};       // file time of the next output frame
    bool ended_{false};
    bool loopNow_{false};
    juce::AudioBuffer<float> readBuffer_;  // reader channels
    juce::AudioBuffer<float> stage_;       // stream channels, interpolator input
    int staged_{0};
    juce::AudioBuffer<float> out_;
    std::vector<juce::LagrangeInterpolator> interpolators_;
    std::vector<juce::dsp::FIR::Filter<float>> antiAlias_;  // per channel
};

}  // namespace spacer
