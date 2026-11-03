#pragma once

#include <vector>

#include "core/Layout.h"
#include "core/MotionSpec.h"
#include "core/Project.h"
#include "core/Source.h"

namespace spacer {

/// Exact (bitwise-intent) equality of model values, out of line. The engine
/// uses these on graph swaps to tell "unchanged" from "edited"; keeping the
/// defaulted float comparisons in core keeps them out of the engine's
/// -Wfloat-equal translation units. No allocation (RT-safe).
bool specEqual(const MotionSpec& a, const MotionSpec& b);
bool specEqual(const std::vector<ModulatorSpec>& a, const std::vector<ModulatorSpec>& b);
bool specEqual(const GeneratorSpec& a, const GeneratorSpec& b);
bool specEqual(const Layout& a, const Layout& b);
bool specEqual(const ClockConfig& a, const ClockConfig& b);

}  // namespace spacer
