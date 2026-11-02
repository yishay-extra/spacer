#include "core/SpecEquality.h"

namespace spacer {

bool specEqual(const MotionSpec& a, const MotionSpec& b) {
    return a == b;
}
bool specEqual(const std::vector<ModulatorSpec>& a, const std::vector<ModulatorSpec>& b) {
    return a == b;
}
bool specEqual(const GeneratorSpec& a, const GeneratorSpec& b) {
    return a == b;
}
bool specEqual(const Layout& a, const Layout& b) {
    return a == b;
}

}  // namespace spacer
