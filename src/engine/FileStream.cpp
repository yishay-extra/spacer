#include "engine/FileStream.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace spacer {

namespace {
constexpr std::size_t kAntiAliasOrder = 256;  // 257 taps, linear phase: delay 128 file frames
constexpr int kAntiAliasDelay = static_cast<int>(kAntiAliasOrder / 2);
constexpr double kAntiAliasCutoff = 0.45;  // × engine rate
// The interpolator's output lags its newest input by `latency` frames;
// priming fills its whole history with real audio.
constexpr int kInterpolatorLatency = static_cast<int>(juce::LagrangeInterpolator::getBaseLatency());
constexpr int kPrimeFrames = 4 * kInterpolatorLatency + 8;
}  // namespace

FileStream::FileStream(std::unique_ptr<juce::AudioFormatReader> reader, const Options& options)
    : reader_(std::move(reader)),
      channels_(std::clamp(options.numChannels, 1, 2)),
      fileRate_(reader_ && reader_->sampleRate > 0.0 ? reader_->sampleRate : options.engineRate),
      engineRate_(options.engineRate > 0.0 ? options.engineRate : 48000.0),
      ratio_(fileRate_ / engineRate_),
      length_(reader_ ? std::max<std::int64_t>(0, reader_->lengthInSamples) : 0),
      startFrame_(std::clamp<std::int64_t>(std::llround(std::max(0.0, options.startSeconds) * fileRate_), 0,
                                           length_)),
      interpolate_(options.forceInterpolation || std::abs(ratio_ - 1.0) > 1e-9),
      loop_(options.loop) {
    const int ringFrames =
        options.ringFrames > 0 ? options.ringFrames : static_cast<int>(engineRate_ * kDefaultRingSeconds);
    ring_.allocate(channels_, std::max(ringFrames, 2 * kChunkFrames));

    const int readerChannels = reader_ ? std::max(1, static_cast<int>(reader_->numChannels)) : 1;
    const int maxInput =
        static_cast<int>(std::ceil(kChunkFrames * ratio_)) + kPrimeFrames + 2 * kAntiAliasDelay + 8;
    readBuffer_.setSize(readerChannels, maxInput);
    stage_.setSize(channels_, 2 * maxInput);
    out_.setSize(channels_, std::max(kChunkFrames, kPrimeFrames + 2 * kAntiAliasDelay));
    interpolators_.resize(static_cast<std::size_t>(channels_));

    if (interpolate_ && ratio_ > 1.0) {
        const auto coefficients = juce::dsp::FilterDesign<float>::designFIRLowpassWindowMethod(
            static_cast<float>(kAntiAliasCutoff * engineRate_), fileRate_, kAntiAliasOrder,
            juce::dsp::WindowingFunction<float>::blackmanHarris);
        for (int c = 0; c < channels_; ++c) antiAlias_.emplace_back(coefficients);
    }

    requestFrame_.store(startFrame_, std::memory_order_relaxed);
    requestGen_.store(0, std::memory_order_release);
}

FileStream::~FileStream() = default;

void FileStream::requestSeek(std::int64_t generation, std::int64_t frame) noexcept {
    requestFrame_.store(std::clamp<std::int64_t>(frame, 0, length_), std::memory_order_relaxed);
    requestGen_.store(generation, std::memory_order_release);
}

void FileStream::restartAt(std::int64_t frame) {
    for (auto& i : interpolators_) i.reset();
    for (auto& f : antiAlias_) f.reset();
    staged_ = 0;
    ended_ = false;
    outPos_ = static_cast<double>(frame);

    if (!interpolate_) {
        filePos_ = frame;
        return;
    }
    // Prime: run kPrimeFrames frames ending `latency` past the seek point
    // (plus the anti-alias filter's delay) through the interpolator at ratio
    // 1 and discard the output. The next output then sits exactly at `frame`.
    const int latency = kInterpolatorLatency + (antiAlias_.empty() ? 0 : kAntiAliasDelay);
    const int prime = kPrimeFrames + (antiAlias_.empty() ? 0 : 2 * kAntiAliasDelay);
    filePos_ = frame + latency - prime;
    readInput(prime);
    for (int c = 0; c < channels_; ++c)
        interpolators_[static_cast<std::size_t>(c)].process(1.0, stage_.getReadPointer(c),
                                                            out_.getWritePointer(c), prime);
    staged_ = 0;
}

void FileStream::readInput(int frames) {
    const int readerChannels = readBuffer_.getNumChannels();
    while (frames > 0) {
        if (loopNow_ && length_ > startFrame_ && filePos_ >= length_) filePos_ = startFrame_;

        int n = frames;
        bool silent = false;
        if (filePos_ < 0) {
            n = static_cast<int>(std::min<std::int64_t>(n, -filePos_));
            silent = true;
        } else if (filePos_ >= length_ || !reader_) {
            silent = true;  // past the end (no loop): zeros for the interpolator tail
        } else {
            n = static_cast<int>(std::min<std::int64_t>(n, length_ - filePos_));
        }
        n = std::min(n, readBuffer_.getNumSamples());

        if (silent) {
            for (int c = 0; c < channels_; ++c) stage_.clear(c, staged_, n);
        } else {
            reader_->read(readBuffer_.getArrayOfWritePointers(), readerChannels, filePos_, n);
            for (int c = 0; c < channels_; ++c) {
                float* dst = stage_.getWritePointer(c, staged_);
                if (channels_ == 1 && readerChannels > 1) {
                    std::fill(dst, dst + n, 0.0f);
                    const float scale = 1.0f / static_cast<float>(readerChannels);
                    for (int r = 0; r < readerChannels; ++r) {
                        const float* src = readBuffer_.getReadPointer(r);
                        for (int i = 0; i < n; ++i) dst[i] += src[i] * scale;
                    }
                } else {
                    const float* src = readBuffer_.getReadPointer(std::min(c, readerChannels - 1));
                    std::copy(src, src + n, dst);
                }
            }
        }
        if (!antiAlias_.empty()) {
            for (int c = 0; c < channels_; ++c) {
                float* x = stage_.getWritePointer(c, staged_);
                auto& f = antiAlias_[static_cast<std::size_t>(c)];
                for (int i = 0; i < n; ++i) x[i] = f.processSample(x[i]);
            }
        }
        staged_ += n;
        filePos_ += n;
        frames -= n;
    }
}

int FileStream::remainingOutput() const noexcept {
    if (loopNow_ && length_ > startFrame_) return 1 << 30;
    const double left = static_cast<double>(length_) - outPos_;
    if (left <= 0.0) return 0;
    return static_cast<int>(std::min(1e9, std::ceil(left / ratio_ - 1e-9)));
}

void FileStream::produce(int frames) {
    if (!interpolate_) {
        readInput(frames);
        for (int c = 0; c < channels_; ++c) out_.copyFrom(c, 0, stage_, c, 0, frames);
        staged_ = 0;
    } else {
        const int needed = static_cast<int>(std::ceil(frames * ratio_)) + 2;
        if (staged_ < needed) readInput(needed - staged_);
        int used = 0;
        for (int c = 0; c < channels_; ++c)
            used = interpolators_[static_cast<std::size_t>(c)].process(ratio_, stage_.getReadPointer(c),
                                                                       out_.getWritePointer(c), frames);
        used = std::min(used, staged_);
        for (int c = 0; c < channels_; ++c) {
            float* s = stage_.getWritePointer(c);
            std::copy(s + used, s + staged_, s);
        }
        staged_ -= used;
    }
    outPos_ += frames * ratio_;
    if (loopNow_ && length_ > startFrame_ && outPos_ >= static_cast<double>(length_))
        outPos_ -= static_cast<double>(length_ - startFrame_);
}

int FileStream::service(int maxFrames) {
    const std::int64_t gen = requestGen_.load(std::memory_order_acquire);
    loopNow_ = loop_.load(std::memory_order_relaxed);
    if (gen != producerGen_) {
        producerGen_ = gen;
        endCount_.store(INT64_MAX, std::memory_order_relaxed);
        restartAt(requestFrame_.load(std::memory_order_relaxed));
        boundary_.store(ring_.writeCount(), std::memory_order_relaxed);
        accepted_.store(gen, std::memory_order_release);
    }

    int written = 0;
    while (!ended_ && written < maxFrames) {
        int n = std::min({ring_.freeSpace(), kChunkFrames, maxFrames - written, remainingOutput()});
        if (remainingOutput() == 0) {
            ended_ = true;
            endCount_.store(ring_.writeCount(), std::memory_order_release);
            break;
        }
        if (n <= 0) break;
        produce(n);
        ring_.write(out_.getArrayOfReadPointers(), n);
        written += n;
        // React to a seek between chunks rather than finishing the fill.
        if (requestGen_.load(std::memory_order_acquire) != producerGen_) break;
    }
    return written;
}

}  // namespace spacer
