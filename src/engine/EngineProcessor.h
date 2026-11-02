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

    EngineProcessor();
    ~EngineProcessor();
    EngineProcessor(const EngineProcessor&) = delete;
    EngineProcessor& operator=(const EngineProcessor&) = delete;

    // ---- control thread ----
    /// False if the queue was full (the command is dropped and counted).
    bool push(const Command& command) noexcept;
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
    void configureClock(const ClockConfig& config) { clock_.configure(config); }

    std::uint64_t droppedCommands() const noexcept { return commands_.dropped(); }
    std::uint64_t droppedFeedback() const noexcept { return feedback_.dropped(); }
    /// Graph latency (limiter look-ahead) of the graph in use, in samples.
    int latencySamples() const noexcept { return latency_.load(std::memory_order_relaxed); }

    // ---- audio thread ----
    /// Renders `numSamples` into `outputs` (every channel is overwritten).
    void process(const float* const* inputs, int numInputs, float* const* outputs, int numOutputs,
                 int numSamples) noexcept;
    /// Device overload count, forwarded as feedback when it changes.
    void reportXruns(int total) noexcept { xruns_ = total; }
    /// The graph in use (audio thread, or while stopped).
    Graph* currentGraph() noexcept { return graph_; }
    ClockState clockState() const noexcept { return clock_.current(); }

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
    void applyCommands() noexcept;
    void apply(const Command& c) noexcept;
    bool applyPendingSwap() noexcept;
    void adoptSampleRate(double sampleRate) noexcept;
    void publishFeedback() noexcept;

    SpscQueue<Command, kCommandCapacity> commands_;
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
    double sampleRate_{0.0};
    GainRamp master_;
    float masterGainDb_{0.0f};
    bool panic_{false};
    std::array<float, kMaxChunk> masterRamp_{};
    std::array<const float*, kMaxIoChannels> inputChunk_{};
    std::array<float*, kMaxIoChannels> outputChunk_{};
    long long samplesToFeedback_{0};
    int xruns_{0}, xrunsReported_{0};
    std::uint64_t droppedReported_{0};

    std::atomic<int> latency_{0};
};

}  // namespace spacer
