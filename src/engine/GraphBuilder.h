#pragma once

#include <functional>
#include <memory>

#include "core/Project.h"
#include "engine/Graph.h"
#include "engine/SourceInput.h"
#include "engine/SpeakerChain.h"

namespace spacer {

/// Builds a ready-to-run Graph from a Project (SPEC §5.2). Non-RT: runs on
/// the message thread (or the offline renderer's thread).
///
/// Speaker chains: effective delay = compensation + user delay, effective
/// gain = compensation + trim (SPEC §7). If the project has a
/// SpeakerChainSpec for a speaker, its trim / user delay / polarity override
/// the layout Speaker's fields (the chain is the live, scene-recallable
/// state; the Speaker fields are the defaults); mute and EQ come only from
/// the chain.
class GraphBuilder {
public:
    using InputFactory = std::function<std::unique_ptr<SourceInput>(const SourceSpec&)>;

    explicit GraphBuilder(Project project);

    /// Creates the input for each source; null (or no factory) = silence.
    GraphBuilder& setInputFactory(InputFactory factory);
    /// Device channels the graph writes; default requiredOutputChannels().
    GraphBuilder& setNumOutputChannels(int numChannels);

    std::unique_ptr<Graph> build(double sampleRate, int maxBlockSize) const;

    const Project& project() const noexcept { return project_; }

    /// 1 + the highest assigned outputChannel (0 if none is assigned).
    static int requiredOutputChannels(const Layout& layout);
    /// Ambisonic bus order for the project: its order if any source is
    /// ambisonic, else 0 (no bus).
    static int ambisonicBusOrder(const Project& project);
    /// The chain settings for every layout speaker, in layout order.
    static std::vector<SpeakerChainSettings> chainSettings(const Project& project);

private:
    Project project_;
    InputFactory inputFactory_;
    int numOutputChannels_{-1};
};

}  // namespace spacer
