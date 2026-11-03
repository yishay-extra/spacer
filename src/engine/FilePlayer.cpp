#include "engine/FilePlayer.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "engine/DiskStreamer.h"

namespace spacer {

FilePlayer::FilePlayer(const FileInputSpec& spec, int numChannels,
                       std::unique_ptr<juce::AudioFormatReader> reader, DiskStreamer* streamer,
                       std::string identity, bool forceInterpolation)
    : spec_(spec),
      channels_(std::clamp(numChannels, 1, 2)),
      reader_(std::move(reader)),
      streamer_(streamer),
      identity_(std::move(identity)),
      forceInterpolation_(forceInterpolation),
      gain_(juce::Decibels::decibelsToGain(spec.gainDb, -200.0f)) {}

FilePlayer::~FilePlayer() = default;

void FilePlayer::prepare(double sampleRate, int maxBlockSize) {
    if (stream_ || !reader_) return;
    FileStream::Options o;
    o.numChannels = channels_;
    o.engineRate = sampleRate;
    o.loop = spec_.loop;
    o.startSeconds = spec_.startSeconds;
    o.ringFrames = std::max(static_cast<int>(sampleRate * FileStream::kDefaultRingSeconds), 8 * maxBlockSize);
    o.forceInterpolation = forceInterpolation_;
    stream_ = std::make_shared<FileStream>(std::move(reader_), o);
    stream_->prime();
    baseFrame_ = stream_->startFrame();
    if (streamer_) streamer_->add(stream_);
}

void FilePlayer::read(const InputContext& /*ctx*/, float* const* dest, int numChannels, int n) noexcept {
    int done = 0;
    if (playing_ && stream_) {
        StreamRing& ring = stream_->ring();
        if (waiting_ && stream_->acceptedGeneration() == generation_) {
            ring.skipTo(stream_->boundary());
            waiting_ = false;
        }
        if (!waiting_) {
            const std::int64_t untilEnd = stream_->endCount() - ring.readCount();
            const int take = static_cast<int>(std::min<std::int64_t>({n, ring.available(), untilEnd}));
            if (take > 0) {
                ring.read(dest, numChannels, take);
                for (int c = 0; c < numChannels; ++c)
                    juce::FloatVectorOperations::multiply(dest[c], gain_, take);
                done = take;
                consumed_ += take;
            }
            if (stream_->endCount() - ring.readCount() <= 0) {
                ended_ = true;
                playing_ = false;
            } else if (done < n) {
                ++underruns_;
            }
        }
    }
    for (int c = 0; c < numChannels; ++c) std::fill(dest[c] + done, dest[c] + n, 0.0f);
}

void FilePlayer::seekFrame(std::int64_t frame) noexcept {
    if (!stream_) return;
    frame = std::clamp<std::int64_t>(frame, 0, stream_->lengthFrames());
    ++generation_;
    stream_->requestSeek(generation_, frame);
    waiting_ = true;
    // Drop what is buffered now; anything the producer writes before it
    // sees the request lies before the new boundary and is skipped later.
    stream_->ring().skipTo(stream_->ring().writeCount());
    baseFrame_ = frame;
    consumed_ = 0;
    ended_ = false;
}

void FilePlayer::play() noexcept {
    if (!stream_) return;
    if (ended_) seekFrame(stream_->startFrame());
    playing_ = true;
}

void FilePlayer::stop() noexcept {
    playing_ = false;
}

void FilePlayer::seek(double seconds) noexcept {
    if (!stream_ || !std::isfinite(seconds)) return;
    seekFrame(std::llround(std::max(0.0, seconds) * stream_->fileRate()));
}

double FilePlayer::positionSeconds() const noexcept {
    if (!stream_) return std::max(0.0, static_cast<double>(spec_.startSeconds));
    const auto length = static_cast<double>(stream_->lengthFrames());
    const auto start = static_cast<double>(stream_->startFrame());
    double pos = static_cast<double>(baseFrame_) + static_cast<double>(consumed_) * stream_->ratio();
    if (pos >= length) {
        if (stream_->loop() && length > start)
            pos = start + std::fmod(pos - length, length - start);
        else
            pos = length;
    }
    return pos / stream_->fileRate();
}

bool FilePlayer::transportState(TransportState& out) const noexcept {
    out.positionSeconds = positionSeconds();
    out.playing = playing_;
    out.underruns = underruns_;
    return true;
}

void FilePlayer::takeStateFrom(SourceInput& old) noexcept {
    auto* o = dynamic_cast<FilePlayer*>(&old);
    if (o == nullptr || o->identity_ != identity_ || !o->stream_ || !stream_) return;
    if (o->stream_->numChannels() == stream_->numChannels() &&
        juce::exactlyEqual(o->stream_->engineRate(), stream_->engineRate()) &&
        o->stream_->startFrame() == stream_->startFrame()) {
        // Pointer swap only (no reference-count traffic on the audio thread);
        // the unused fresh stream goes back with the old graph.
        std::swap(stream_, o->stream_);
        o->handedOver_ = true;
        stream_->setLoop(spec_.loop);
        generation_ = o->generation_;
        waiting_ = o->waiting_;
        playing_ = o->playing_;
        ended_ = o->ended_;
        baseFrame_ = o->baseFrame_;
        consumed_ = o->consumed_;
        underruns_ = o->underruns_;
        return;
    }
    // Another rate (or layout of the stream): continue from the old position.
    const bool wasPlaying = o->playing_;
    const bool wasEnded = o->ended_;
    seek(o->positionSeconds());
    ended_ = wasEnded;
    playing_ = wasPlaying;
}

}  // namespace spacer
