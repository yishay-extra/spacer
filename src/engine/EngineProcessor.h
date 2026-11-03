#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>

#include "core/InternalClock.h"
#include "engine/Command.h"
#include "engine/Feedback.h"
#include "engine/GainRamp.h"
#include "engine/Graph.h"
#include "engine/RecallPlan.h"
#include "engine/Seqlock.h"
#include "engine/SpscQueue.h"

namespace spacer {

/// The real-time heart of the engine, independent of any audio device: owns
/// the current Graph, the command / feedback / retire queues, the clock and
/// the master stage (SPEC §5). AudioEngine drives it from the device
/// callback; OfflineRenderer drives it directly.
///
/// Threading: push(), swapGraph(), popFeedback() and collectGarbage() are for
/// ONE control thread (the message thread). process() is for the audio
/// thread. setGraphWhileStopped() and the destructor require that process()
/// is not running.
class EngineProcessor {
public:
    static constexpr int kCommandCapacity = 4096;  // SPEC §5.2
    static constexpr int kFeedbackCapacity = 8192;
    static constexpr int kRetireCapacity = 32;
    static constexpr int kMaxIoChannels = 256;
    static constexpr int kMaxChunk = 2048;  // SPEC §1 max block size
    static constexpr double kFeedbackRateHz = 60.0;
    static constexpr int kRetirePlanCapacity = 256;
    static constexpr int kMaxActiveRamps = 2048;
    /// Crossfade for discrete switches during a recall (SPEC §11).
    static constexpr float kRecallCrossfadeMs = 50.0f;
    /// Timed commands (pushAt) waiting for their sample.
    static constexpr int kMaxTimedCommands = 1024;

    /// The engine timeline at the start of the last processed block, published
    /// by the audio thread (seqlock: reads from the message thread are exact).
    struct Snapshot {
        ClockState clock;
        /// Engine sample index of the block's first sample (samples processed
        /// before it). The same timeline pushAt() uses.
        std::int64_t blockStartSample{0};
        /// Host time (seconds, juce::Time::getMillisecondCounterHiRes() / 1000)
        /// of that block's callback, or < 0 if the driver gave none.
        double hostTimeSeconds{-1.0};
        double sampleRate{0.0};
    };

    EngineProcessor();
    ~EngineProcessor();
    EngineProcessor(const EngineProcessor&) = delete;
    EngineProcessor& operator=(const EngineProcessor&) = delete;

    // ---- control thread ----
    /// False if the queue was full (the command is dropped and counted).
    bool push(const Command& command) noexcept;
    /// Like push(), but the command takes effect exactly at engine sample
    /// `engineSample` (Snapshot::blockStartSample timeline): the block is
    /// split there. A sample that has already passed applies at the next
    /// block, like push(). Clock transport commands take effect at the start
    /// of the block after their sample (SPEC §9: the clock advances first).
    /// For OSC bundle timetags (SPEC §12.1).
    bool pushAt(const Command& command, std::int64_t engineSample) noexcept;
    /// Engine sample at host time `hostSeconds` (same clock as
    /// Snapshot::hostTimeSeconds), extrapolated from the last block; -1 if
    /// the engine has no host time yet (offline, or not started).
    std::int64_t sampleAtHostTime(double hostSeconds) const noexcept;
    /// Hands `graph` to the audio thread. If the queue is full, the graph is
    /// deleted and false returned.
    bool swapGraph(std::unique_ptr<Graph> graph);
    bool popFeedback(Feedback& out) noexcept { return feedback_.pop(out); }
    /// Deletes graphs and recall plans the audio thread has finished with.
    /// Call regularly.
    int collectGarbage();
    /// Only while process() cannot run (device stopped / before start).
    void setGraphWhileStopped(std::unique_ptr<Graph> graph);
    /// Only while process() cannot run. Live changes go through commands.
    /// Configures the internal clock; external clocks travel with a graph
    /// (Graph::setClock, SPEC §9).
    void configureClock(const ClockConfig& config) { clock_.configure(config); }

    std::uint64_t droppedCommands() const noexcept { return commands_.dropped(); }
    std::uint64_t droppedFeedback() const noexcept { return feedback_.dropped(); }
    /// Graph latency (limiter look-ahead) of the graph in use, in samples.
    int latencySamples() const noexcept { return latency_.load(std::memory_order_relaxed); }

    // ---- audio thread ----
    /// Renders `numSamples` into `outputs` (every channel is overwritten).
    /// `virtualInputs` (optional) are the Spacer.driver channels for this
    /// block (VirtualDeviceReceiver::channels()), read by VirtualInput sources.
    void process(const float* const* inputs, int numInputs, float* const* outputs, int numOutputs,
                 int numSamples, const float* const* virtualInputs = nullptr,
                 int numVirtualInputs = 0) noexcept;
    /// Device overload count, forwarded as feedback when it changes.
    void reportXruns(int total) noexcept { xruns_ = total; }
    /// Host time of the callback about to call process() (seconds on
    /// juce::Time::getMillisecondCounterHiRes() / 1000). Optional; enables
    /// sampleAtHostTime().
    void setBlockHostTime(double hostSeconds) noexcept { nextHostTime_ = hostSeconds; }
    /// The graph in use (audio thread, or while stopped).
    Graph* currentGraph() noexcept { return graph_; }
    /// State of the active clock (internal or the graph's external one) at
    /// the start of the last block. Any thread (seqlock).
    ClockState clockState() const noexcept { return snapshot_.load().clock; }
    /// Clock + timeline of the last block. Any thread (seqlock).
    Snapshot snapshot() const noexcept { return snapshot_.load(); }
    /// True while an external clock (from the graph) is active.
    bool externalClockActive() const noexcept { return external_ != nullptr; }

private:
    struct ActiveRamp {
        Ramp ramp;
        float from{0.0f};
        Position fromPosition;
        int total{0};
        int elapsed{0};
    };

    void applyRecall(RecallPlan* plan) noexcept;
    void startRamp(const Ramp& ramp, int totalSamples, float rampMs) noexcept;
    void applyRampValue(const Ramp& ramp, float value, const Position& position) noexcept;
    void stepRamps(int numSamples) noexcept;
    void retirePlan(RecallPlan* plan) noexcept;
    void scheduleRecall(RecallPlan* plan) noexcept;
    int scheduledRecallOffset(const ClockState& clock, int numSamples) const noexcept;
    struct QueuedCommand {
        Command command;
        std::int64_t atSample{-1};  // -1 = as soon as possible
    };
    void applyCommands() noexcept;
    void insertTimed(const QueuedCommand& q) noexcept;
    void applyTimedUpTo(std::int64_t sample) noexcept;
    int nextTimedOffset(std::int64_t blockStart, int numSamples) const noexcept;
    void apply(const Command& c) noexcept;
    bool applyPendingSwap() noexcept;
    void adoptSampleRate(double sampleRate) noexcept;
    /// After graph_ changed: make the graph's clock selection the active one.
    void selectClock() noexcept;
    ClockState advanceClock(int numSamples) noexcept;
    void publishFeedback() noexcept;

    SpscQueue<QueuedCommand, kCommandCapacity> commands_;
    SpscQueue<Feedback, kFeedbackCapacity> feedback_;
    SpscQueue<Graph*, kRetireCapacity> retired_;
    SpscQueue<RecallPlan*, kRetirePlanCapacity> retiredPlans_;

    // Audio-thread state.
    Graph* graph_{nullptr};
    Graph* pending_{nullptr};
    RecallPlan* scheduled_{nullptr};  // AtBeat recall waiting for its beat
    std::array<ActiveRamp, kMaxActiveRamps> ramps_{};
    int numRamps_{0};
    bool fadingOut_{false};
    InternalClock clock_;
    ClockSource* external_{nullptr};  // graph_'s external clock, or null = clock_
    ClockState clockState_;
    double sampleRate_{0.0};
    GainRamp master_;
    float masterGainDb_{0.0f};
    bool panic_{false};
    std::array<float, kMaxChunk> masterRamp_{};
    std::array<const float*, kMaxIoChannels> inputChunk_{};
    std::array<const float*, kMaxIoChannels> virtualChunk_{};
    std::array<float*, kMaxIoChannels> outputChunk_{};
    long long samplesToFeedback_{0};
    int xruns_{0}, xrunsReported_{0};
    std::uint64_t droppedReported_{0};

    std::array<QueuedCommand, kMaxTimedCommands> timed_{};  // sorted by atSample
    int numTimed_{0};
    std::int64_t samplePosition_{0};
    double nextHostTime_{-1.0};
    Seqlock<Snapshot> snapshot_;

    std::atomic<int> latency_{0};
};

}  // namespace spacer
