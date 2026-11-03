#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "vdev/Protocol.h"
#include "vdev/RingReader.h"
#include "vdev/SharedMemory.h"

namespace spacer {

/// The app side of Spacer.driver (SPEC §14): maps the driver's shared-memory
/// ring and turns it into kNumChannels blocks of audio at the engine's rate,
/// once per device callback. VirtualInput sources then pick their channels
/// from channels() (through InputContext::virtualInputs).
///
/// Drift: the driver runs on the host clock at its nominal rate, the engine on
/// the output device's clock. A fill-level-steered resampler absorbs the
/// difference (SPEC §14): the read ratio is
///     nominal · (1 + clamp(e / (sr · Tc) + ∫ e / (sr · Tc · Ti) dt, ±kMaxAdjust)),
/// nominal = driverRate / engineRate, e = smoothed (fill − target) in frames
/// (one-pole, kFillSmoothingSeconds), Tc = kCorrectionSeconds,
/// Ti = kIntegralSeconds (PI: no steady-state fill offset under drift). Fill is measured before each read:
/// the unread frames plus the frames the sender has produced since its last
/// write (from the write's host time; the ring fills in steps of the sender's
/// buffer, and this keeps the measure continuous). The target is
///     target = n · nominal + max(latencyFrames, sender buffer) + kSafetyFrames
/// so that after a read about one driver buffer remains (SPEC §14 added
/// latency) and the lowest point of the sender's write cycle still covers a
/// block. Resampling is juce's Lagrange interpolator (all channels share one
/// ratio, so they stay aligned).
///
/// States: no ring → silence (Absent / Incompatible). Priming → silence
/// until the fill reaches target, then the backlog beyond target is dropped
/// and playback starts. An underrun (fill < what the block needs) outputs
/// silence for that block, counts the frames (only while the driver reports
/// IO running) and re-primes. A backlog far beyond target (stall) is dropped
/// (resync).
///
/// Threading: the constructor, prepare(), poll(), status() and the
/// destructor are for the message thread (prepare() only while pull() cannot
/// run); pull() and channels() are for the audio thread. The mapping can
/// change under a running callback (driver loaded, coreaudiod restarted): the
/// audio thread announces the connection it uses (hazard pointer) and poll()
/// frees retired connections only when the audio thread is not using them.
class VirtualDeviceReceiver {
public:
    static constexpr int kNumChannels = static_cast<int>(vdev::kNumChannels);
    static constexpr double kMaxNominalRatio = 4.0;
    static constexpr double kMaxAdjust = 0.002;  // ±2000 ppm
    static constexpr double kCorrectionSeconds = 0.5;
    static constexpr double kFillSmoothingSeconds = 0.25;
    static constexpr double kIntegralSeconds = 5.0;
    static constexpr double kRetrySeconds = 1.0;
    /// Headroom over the target for the interpolator and scheduling jitter.
    static constexpr int kSafetyFrames = 32;

    enum class State {
        Absent,         // no shared memory: driver not installed or not loaded
        Incompatible,   // a driver whose protocol this app does not speak
        NotResponding,  // mapped, but the driver's heartbeat stopped
        Connected,
    };

    struct Status {
        State state{State::Absent};
        std::string message;        // human-readable, for the status bar / warnings
        std::string driverVersion;  // from the shared header ("" if unknown)
        std::uint32_t protocolVersion{0};
        double driverSampleRate{0.0};
        bool driverRunning{false};  // a sender is doing IO on the device
        bool playing{false};        // primed and delivering audio
        int fillFrames{0};
        int latencyFrames{0};
        double ratio{1.0};
        std::uint64_t underrunFrames{0};  // counted by this receiver
        std::uint64_t overrunFrames{0};   // counted by the driver
        std::uint64_t resyncs{0};
    };

    explicit VirtualDeviceReceiver(std::string sharedMemoryName = vdev::kSharedMemoryName);
    ~VirtualDeviceReceiver();
    VirtualDeviceReceiver(const VirtualDeviceReceiver&) = delete;
    VirtualDeviceReceiver& operator=(const VirtualDeviceReceiver&) = delete;

    // ---- message thread ----
    /// Allocates for blocks up to `maxBlockSize` at `sampleRate` and asks the
    /// driver to follow that rate. Only while pull() cannot run.
    void prepare(double sampleRate, int maxBlockSize);
    /// Added latency in frames (SPEC §14: 128 / 256 / 512; default 512).
    void setLatencyFrames(int frames) noexcept;
    int latencyFrames() const noexcept { return latency_.load(std::memory_order_relaxed); }
    /// Call regularly (the engine's 30 Hz timer): (re)connects, watches the
    /// driver heartbeat, frees retired mappings. `nowSeconds` is a monotonic
    /// clock (overload without it uses std::chrono::steady_clock). Returns
    /// true when the state changed.
    bool poll();
    bool poll(double nowSeconds);
    Status status() const;
    State state() const noexcept { return state_; }
    /// The largest block pull() can produce (as prepared).
    int maxBlockSize() const noexcept { return maxBlock_; }

    // ---- audio thread ----
    /// Produces `numSamples` frames on every channel. At most maxBlockSize()
    /// are produced; callers must not ask for more.
    void pull(int numSamples) noexcept;
    /// Same, with the current time on the protocol clock (vdev::monotonicNowNs()).
    void pull(int numSamples, std::uint64_t nowNs) noexcept;
    /// kNumChannels pointers to the last pull()'s frames.
    const float* const* channels() const noexcept { return outPtrs_.data(); }

private:
    struct Connection {
        vdev::SharedMemory memory;
        vdev::RingReader reader;
        std::uint64_t sessionId{0};
    };

    std::unique_ptr<Connection> openConnection(std::string& message, State& failState) const;
    void install(std::unique_ptr<Connection> connection);
    void retire();
    void freeRetired() noexcept;
    void silence(int from, int numSamples) noexcept;
    void requestRate(Connection& c) const noexcept;

    const std::string name_;

    // Message-thread state.
    State state_{State::Absent};
    std::string message_;
    std::unique_ptr<Connection> owned_;  // the connection active_ points at
    std::vector<std::unique_ptr<Connection>> retired_;
    double nextRetry_{0.0};
    std::uint64_t lastHeartbeat_{0};
    double heartbeatChanged_{0.0};
    double sampleRate_{0.0};

    // Shared with the audio thread.
    std::atomic<Connection*> active_{nullptr};
    std::atomic<Connection*> inUse_{nullptr};
    std::atomic<int> latency_{static_cast<int>(vdev::kDefaultLatencyFrames)};
    std::atomic<int> fill_{0};
    std::atomic<double> ratio_{1.0};
    std::atomic<bool> playing_{false};
    std::atomic<std::uint64_t> underruns_{0};
    std::atomic<std::uint64_t> resyncs_{0};

    // Audio-thread state.
    Connection* current_{nullptr};
    bool priming_{true};
    double fillError_{0.0};
    double integral_{0.0};
    int maxBlock_{0};
    int scratchFrames_{0};
    std::vector<float> out_;      // kNumChannels × maxBlock_
    std::vector<float> scratch_;  // kNumChannels × scratchFrames_
    std::array<float*, kNumChannels> outPtrs_{};
    std::array<float*, kNumChannels> scratchPtrs_{};
    std::array<juce::LagrangeInterpolator, kNumChannels> resamplers_;
};

}  // namespace spacer
