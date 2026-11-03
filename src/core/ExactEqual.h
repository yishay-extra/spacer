#pragma once

// IEEE == for value-type operator==. Spelled via std::equal_to because a
// direct == on floating types trips -Wfloat-equal; the comparison is
// instantiated inside the standard library, where the warning does not fire.

#include <concepts>
#include <functional>

namespace spacer {

template <std::floating_point T>
constexpr bool exactlyEqual(T a, T b) noexcept {
    return std::equal_to<T>{}(a, b);
}

}  // namespace spacer
