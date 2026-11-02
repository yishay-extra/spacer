#pragma once

#include <juce_core/juce_core.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "core/Project.h"
#include "engine/Graph.h"
#include "engine/SourceInput.h"
#include "engine/SpeakerChain.h"

namespace spacer {

class DiskStreamer;

/// Builds a ready-to-run Graph from a Project (SPEC §5.2). Non-RT: runs on
/// the message thread (or the offline renderer's thread).
///
/// Speaker chains: effective delay = compensation + user delay, effective
/// gain = compensation + trim (SPEC §7). If the project has a
/// SpeakerChainSpec for a speaker, its trim / user delay / polarity override
/// the layout Speaker's fields (the chain is the live, scene-recallable
/// state; the Speaker fields are the defaults); mute and EQ come only from
/// the chain.
///
/// Source inputs: the input factory (if any) is asked first; when it returns
/// null the source gets its live input from SourceInputFactory (hardware,
/// file, generator; SPEC §8). File players are streamed by the DiskStreamer
/// given with setDiskStreamer().
class GraphBuilder {
public:
    using InputFactory = std::function<std::unique_ptr<SourceInput>(const SourceSpec&)>;

    explicit GraphBuilder(Project project);

    /// Overrides the input of some sources (tests, offline renders, tools);
    /// null = the source's own live input.
    GraphBuilder& setInputFactory(InputFactory factory);
    /// Disk thread for file sources. Must outlive the built graphs.
    GraphBuilder& setDiskStreamer(DiskStreamer* streamer);
    /// Base directory for project-relative file paths.
    GraphBuilder& setProjectDirectory(const juce::File& directory);
    /// Device channels the graph writes; default requiredOutputChannels().
    GraphBuilder& setNumOutputChannels(int numChannels);

    /// `warnings` (optional) receives non-fatal problems, e.g. missing files.
    std::unique_ptr<Graph> build(double sampleRate, int maxBlockSize,
                                 std::vector<std::string>* warnings = nullptr) const;

    const Project& project() const noexcept { return project_; }

    /// 1 + the highest assigned outputChannel (0 if none is assigned).
    static int requiredOutputChannels(const Layout& layout);
    /// 1 + the highest device input channel a HardwareInput source reads (0 if none).
    static int requiredInputChannels(const Project& project);
    /// Ambisonic bus order for the project: its order if any source is
    /// ambisonic, else 0 (no bus).
    static int ambisonicBusOrder(const Project& project);
    /// The chain settings for every layout speaker, in layout order.
    static std::vector<SpeakerChainSettings> chainSettings(const Project& project);

private:
    Project project_;
    InputFactory inputFactory_;
    DiskStreamer* streamer_{nullptr};
    juce::File projectDirectory_;
    int numOutputChannels_{-1};
};

}  // namespace spacer
