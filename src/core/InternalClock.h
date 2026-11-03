#pragma once

#include <array>

#include "core/Clock.h"
#include "core/Project.h"

namespace spacer {

/// The v1 clock: tempo, transport and tap tempo, driven by sample counts. See
/// SPEC §9.
///
/// Transport model:
///  - `advance(n)` moves the beat by n samples' worth of time while playing and
///    returns the state at the START of the block (ClockSource contract).
///  - `stop()` freezes the beat. `resume()` restarts from the frozen beat.
///  - `play()` starts from the current beat. If `quantisedStart` is set it
///    arms instead, and the transport starts at the next bar boundary, which
///    may be inside the block being rendered. While armed the reported beat
///    stays frozen at the position it was at when play() was called.
///  - A bar boundary exactly at the current beat counts as "next", so the
///    quantised start happens immediately.
///  - `ClockState::startOffset` / `startBeat` (SPEC §9) report where in the
///    block the transport starts: sample 0 for `play()` (unquantised) and
///    `resume()`, the bar boundary's sample for a quantised start.
///  - `tap(t)` takes tap times in engine seconds (the same timeline as
///    ClockState::timeSeconds).
///
/// Not thread-safe. Owned by the audio thread; commands reach it through the
/// engine's command queue. No allocation, locks, or I/O anywhere.
class InternalClock final : public ClockSource {
public:
    static constexpr double kMinBpm = 20.0;
    static constexpr double kMaxBpm = 300.0;
    /// A tap more than this long after the previous one starts a new tap series.
    static constexpr double kTapTimeoutSeconds = 2.0;
    /// Number of most recent tap intervals averaged.
    static constexpr int kTapHistory = 4;

    explicit InternalClock(double sampleRate = 48000.0, int beatsPerBar = 4);

    /// Applies bpm, beatsPerBar and quantisedStart from the project file.
    void configure(const ClockConfig& config);

    void setSampleRate(double sampleRate) override;
    /// Clamped to [kMinBpm, kMaxBpm]. Beat position is kept across the change.
    void setBpm(double bpm);
    /// Clamped to >= 1.
    void setBeatsPerBar(int beatsPerBar);
    void setQuantisedStart(bool quantised) { quantisedStart_ = quantised; }
    /// Ignored if not finite. Re-targets a pending quantised start.
    void setBeat(double beat);

    /// Starts the transport, quantised to the next bar if configured. No-op if
    /// already playing.
    void play();
    /// Starts immediately from the current beat, ignoring quantisation.
    void resume();
    /// Stops the transport and cancels any pending quantised start.
    void stop();

    /// Registers a tap at engine time `timeSeconds` and updates the tempo from
    /// the average of recent tap intervals. Needs at least two taps.
    void tap(double timeSeconds);

    ClockState advance(int numSamples) override;
    ClockState current() const override { return last_; }

    /// True while a quantised start is waiting for its bar boundary.
    bool armed() const { return armed_; }

private:
    void arm(double fromBeat);
    double nextBarBoundary(double beat) const;
    double beatsForSamples(int numSamples) const;

    ClockState state_;
    ClockState last_;
    bool quantisedStart_{false};
    bool armed_{false};
    bool startNow_{false};  // play()/resume() started the transport; reported at sample 0 of the next block
    double pendingBeat_{0.0};     // bar boundary the quantised start aims for
    double armedBeatsToGo_{0.0};  // beats left until pendingBeat_, counted down per block

    bool haveTap_{false};
    double lastTap_{0.0};
    int tapIntervalCount_{0};
    int nextTapInterval_{0};
    std::array<double, kTapHistory> tapIntervals_{};
};

}  // namespace spacer
