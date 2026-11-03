#include "engine/ClockSelector.h"

#include "core/SpecEquality.h"

namespace spacer {

bool ClockSelector::apply(Graph& graph, const ClockConfig& config) {
    // The latency trim is read live (ClockInputs::setCompensation); changing
    // it must not recreate (and relock) the clock.
    ClockConfig same = config;
    same.latencyTrimMs = current_.latencyTrimMs;
    if (known_ && specEqual(same, current_)) {
        current_ = config;
        return false;
    }
    std::unique_ptr<ClockSource> clock;
    if (config.kind != ClockKind::Internal && factory_) clock = factory_(config);
    activeKind_ = clock ? config.kind : ClockKind::Internal;
    graph.setClock(std::move(clock));
    current_ = config;
    known_ = true;
    return true;
}

}  // namespace spacer
