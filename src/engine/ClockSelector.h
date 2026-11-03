#pragma once

#include <functional>
#include <memory>

#include "core/Clock.h"
#include "core/Project.h"
#include "engine/Graph.h"

namespace spacer {

/// Decides which clock a newly built graph carries (SPEC §9: only one clock
/// is active and switching happens through SwapGraph). Message thread.
///
/// The clock is replaced only when the project's ClockConfig changes:
///  - kind Internal: the graph selects the engine's internal clock;
///  - any other kind: the factory creates the external ClockSource (a null
///    result, e.g. Link not compiled in, falls back to the internal clock);
///  - unchanged config: the graph's clock is left unset, so it inherits the
///    running clock with all its state (lock, tempo estimate, position).
/// Configuration changes of an external clock (tempo used by MTC, beats per
/// bar) recreate it; an external clock relocks within a few frames / beats.
class ClockSelector {
public:
    using Factory = std::function<std::unique_ptr<ClockSource>(const ClockConfig&)>;

    void setFactory(Factory factory) { factory_ = std::move(factory); }

    /// Call for every graph that is about to be swapped in, in order.
    /// Returns true if the graph carries a clock change.
    bool apply(Graph& graph, const ClockConfig& config);

    /// Forget what the engine runs (e.g. the engine was rebuilt from scratch):
    /// the next apply() sets the clock explicitly.
    void invalidate() { known_ = false; }

    /// The kind actually selected by the last change (Internal after a
    /// fallback).
    ClockKind activeKind() const noexcept { return activeKind_; }

private:
    Factory factory_;
    ClockConfig current_;
    bool known_{true};  // a fresh engine runs the internal clock with the default config
    ClockKind activeKind_{ClockKind::Internal};
};

}  // namespace spacer
