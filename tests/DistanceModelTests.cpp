#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>

#include "core/spatial/DistanceModel.h"

using Catch::Approx;
using namespace spacer;

TEST_CASE("Distance gain follows (1 / max(d, rMin))^k") {
    DistanceModel m;  // k = 1, rMin = 0.5
    CHECK(distanceGain(m, 1.0f) == Approx(1.0f));
    CHECK(distanceGain(m, 2.0f) == Approx(0.5f));
    CHECK(distanceGain(m, 4.0f) == Approx(0.25f));
    CHECK(distanceGain(m, 0.1f) == Approx(2.0f));  // clamped to rMin = 0.5
    CHECK(distanceGain(m, 0.0f) == Approx(2.0f));
    m.k = 2.0f;
    CHECK(distanceGain(m, 2.0f) == Approx(0.25f));
    m.k = 0.0f;
    CHECK(distanceGain(m, 10.0f) == 1.0f);
}

TEST_CASE("Distance cutoff falls with distance when q > 0") {
    DistanceModel m;
    CHECK(distanceCutoffHz(m, 8.0f) == Approx(20000.0f));  // q = 0: off
    m.q = 1.0f;
    CHECK(distanceCutoffHz(m, 0.5f) == Approx(20000.0f));  // clamped at r_ref
    CHECK(distanceCutoffHz(m, 1.0f) == Approx(20000.0f));
    CHECK(distanceCutoffHz(m, 4.0f) == Approx(5000.0f));
}

TEST_CASE("DistanceProcessor applies gain, ramped across the block") {
    DistanceProcessor p;
    p.prepare(48000.0);
    DistanceModel m;
    std::vector<float> x(100, 1.0f);
    p.process(x.data(), 100, 1.0f, 2.0f, m);
    CHECK(x[0] == Approx(1.0f));
    CHECK(x[50] == Approx(0.75f));
    CHECK(x[99] == Approx(0.505f));
}

TEST_CASE("DistanceProcessor low-pass: DC passes, Nyquist-rate content is attenuated") {
    DistanceModel m;
    m.k = 0.0f;
    m.q = 1.0f;
    DistanceProcessor p;
    p.prepare(48000.0);

    std::vector<float> dc(4800, 1.0f);
    p.process(dc.data(), 4800, 16.0f, 16.0f, m);  // fc = 1.25 kHz
    CHECK(dc.back() == Approx(1.0f).margin(1e-3));

    p.reset();
    std::vector<float> alt(4800);
    for (std::size_t i = 0; i < alt.size(); ++i) alt[i] = (i % 2) ? -1.0f : 1.0f;
    p.process(alt.data(), 4800, 16.0f, 16.0f, m);
    CHECK(std::abs(alt.back()) < 0.1f);
}

TEST_CASE("DistanceProcessor with q = 0 leaves the spectrum alone") {
    DistanceModel m;
    m.k = 0.0f;
    DistanceProcessor p;
    p.prepare(48000.0);
    std::vector<float> x{1.0f, -1.0f, 1.0f, -1.0f};
    p.process(x.data(), 4, 8.0f, 8.0f, m);
    CHECK(x == std::vector<float>{1.0f, -1.0f, 1.0f, -1.0f});
}
