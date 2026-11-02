#pragma once

#include <span>

#include "core/Position.h"

namespace spacer {

inline constexpr int kMinAmbisonicOrder = 1;
inline constexpr int kMaxAmbisonicOrder = 5;
inline constexpr int kMaxAmbisonicChannels = (kMaxAmbisonicOrder + 1) * (kMaxAmbisonicOrder + 1);

/// Number of ambisonic channels for an order: (N+1)^2.
inline constexpr int ambisonicChannels(int order) {
    return (order + 1) * (order + 1);
}

/// Degree l of ACN channel n (l^2 <= n < (l+1)^2).
int acnDegree(int acn);

/// Real spherical harmonics up to `order` (0..kMaxAmbisonicOrder) for a
/// direction, ACN channel order, SN3D normalisation, no Condon-Shortley phase
/// (AmbiX). Ambisonic azimuth runs counter-clockwise from the front, so ACN 1
/// (Y) is positive for sources on the left and ACN 3 (X) for sources in front.
/// `direction` need not be unit length; the zero vector is treated as front.
/// Writes ambisonicChannels(order) values. RT-safe.
void sphericalHarmonicsSN3D(int order, const Cartesian& direction, float* out);

/// max-rE per-degree weights a_0..a_N (a_0 = 1): a_l = P_l(r_N), r_N the
/// largest root of the Legendre polynomial P_{N+1} (Zotter & Frank 2012).
/// Writes order+1 values. RT-safe.
void maxReWeights(int order, double* out);

}  // namespace spacer
