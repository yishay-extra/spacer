#include "engine/ClockSelector.h"

namespace spacer {

bool ClockSelector::apply(Graph& graph, const ClockConfig& config) {
    if (known_ && config == current_) return false;
    std::unique_ptr<ClockSource> clock;
    if (config.kind != ClockKind::Internal && factory_) clock = factory_(config);
    activeKind_ = clock ? config.kind : ClockKind::Internal;
    graph.setClock(std::move(clock));
    current_ = config;
    known_ = true;
    return true;
}

}  // namespace spacer
