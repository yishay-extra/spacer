#include "core/spatial/SphericalHarmonics.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace spacer {

namespace {
/// Legendre polynomial P_l(x) by the Bonnet recurrence; also returns P_l'(x)
/// through `deriv` when non-null (|x| < 1).
double legendre(int l, double x, double* deriv = nullptr) {
    double p0 = 1.0, p1 = x;
    if (l == 0) {
        if (deriv) *deriv = 0.0;
        return 1.0;
    }
    for (int k = 2; k <= l; ++k) {
        const double p2 = ((2.0 * k - 1.0) * x * p1 - (k - 1.0) * p0) / k;
        p0 = p1;
        p1 = p2;
    }
    if (deriv) *deriv = l * (x * p1 - p0) / (x * x - 1.0);
    return p1;
}
}  // namespace

int acnDegree(int acn) {
    int l = 0;
    while ((l + 1) * (l + 1) <= acn) ++l;
    return l;
}

void sphericalHarmonicsSN3D(int order, const Cartesian& direction, float* out) {
    order = std::clamp(order, 0, kMaxAmbisonicOrder);
    // Ambisonic axes: X front, Y left, Z up (Spacer: x right, y front, z up).
    double xa = direction.y, ya = -static_cast<double>(direction.x), za = direction.z;
    const double len = std::sqrt(xa * xa + ya * ya + za * za);
    if (len > 0.0) {
        xa /= len;
        ya /= len;
        za /= len;
    } else {
        xa = 1.0;
        ya = za = 0.0;
    }

    // Y_l^m = N_l^|m| · P̄_l^|m|(z) · (Re or Im of (xa + i·ya)^|m|), where
    // P̄ = P_l^m / ρ^m (ρ = cos el) is a plain polynomial in z, and
    // (xa + i·ya)^m = ρ^m·e^{imφ}. Exact at the poles, no trigonometry.
    double c[kMaxAmbisonicOrder + 1], s[kMaxAmbisonicOrder + 1];
    c[0] = 1.0;
    s[0] = 0.0;
    for (int m = 1; m <= order; ++m) {
        c[m] = c[m - 1] * xa - s[m - 1] * ya;
        s[m] = c[m - 1] * ya + s[m - 1] * xa;
    }

    for (int m = 0; m <= order; ++m) {
        // P̄_m^m = (2m-1)!!, P̄_{m+1}^m = z(2m+1)P̄_m^m, then the usual
        // three-term recurrence in l.
        double pmm = 1.0;
        for (int k = 1; k <= m; ++k) pmm *= 2.0 * k - 1.0;
        double pPrev = 0.0, p = pmm;
        for (int l = m; l <= order; ++l) {
            if (l == m + 1) {
                pPrev = p;
                p = za * (2.0 * m + 1.0) * pmm;
            } else if (l > m + 1) {
                const double next = ((2.0 * l - 1.0) * za * p - (l + m - 1.0) * pPrev) / (l - m);
                pPrev = p;
                p = next;
            }
            // SN3D: N = sqrt((2 - δ_m0)·(l-m)!/(l+m)!).
            double ratio = 1.0;
            for (int k = l - m + 1; k <= l + m; ++k) ratio /= k;
            const double norm = std::sqrt((m == 0 ? 1.0 : 2.0) * ratio);
            const int base = l * l + l;
            out[base + m] = static_cast<float>(norm * p * c[m]);
            if (m > 0) out[base - m] = static_cast<float>(norm * p * s[m]);
        }
    }
}

void maxReWeights(int order, double* out) {
    order = std::clamp(order, 0, kMaxAmbisonicOrder);
    // Largest root of P_{N+1}: Newton from the standard Gauss-Legendre guess.
    const int n = order + 1;
    double x = std::cos(std::numbers::pi * 0.75 / (n + 0.5));
    for (int it = 0; it < 50; ++it) {
        double d = 0.0;
        const double p = legendre(n, x, &d);
        const double dx = p / d;
        x -= dx;
        if (std::abs(dx) < 1e-15) break;
    }
    for (int l = 0; l <= order; ++l) out[l] = legendre(l, x);
}

}  // namespace spacer
