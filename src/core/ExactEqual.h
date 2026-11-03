#pragma once

// Exact floating-point equality for value-type operator==. Written with
// ordering comparisons so it stays clean under -Wfloat-equal. Differs from
// == only for NaN, where two NaNs compare equal here.

namespace spacer {

template <typename T>
constexpr bool exactlyEqual(T a, T b) noexcept {
    return !(a < b) && !(b < a);
}

}  // namespace spacer
