#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

#include "ClockSimUtil.h"
#include "core/clock/TimeDll.h"

using namespace spacer;
using Catch::Approx;

TEST_CASE("TimeDll: exact periodic input stays exact", "[clock][dll]") {
    TimeDll dll(1.0);
    dll.reset(10.0, 0.01);
    for (int i = 1; i <= 1000; ++i)
        CHECK(dll.update(10.0 + i * 0.01) == Approx(10.0 + i * 0.01).margin(1e-12));
    CHECK(dll.period() == Approx(0.01).margin(1e-12));
}

TEST_CASE("TimeDll: converges to the true period from a wrong start", "[clock][dll]") {
    TimeDll dll(2.0);
    dll.reset(0.0, 0.011);  // 10 % off
    double t = 0.0;
    for (int i = 0; i < 2000; ++i) {
        t += 0.01;
        dll.update(t);
    }
    CHECK(dll.period() == Approx(0.01).epsilon(1e-6));
    CHECK(dll.time() == Approx(t).margin(1e-6));
}

TEST_CASE("TimeDll: filters jitter", "[clock][dll]") {
    testing::Jitter jitter(0.001, 3);  // +-1 ms on a 20.8 ms period
    TimeDll dll(0.5);
    const double period = 60.0 / 120.0 / 24.0;
    dll.reset(0.0, period);
    double maxErr = 0.0, sumSq = 0.0;
    int n = 0;
    for (int i = 1; i <= 5000; ++i) {
        const double truth = i * period;
        const double filtered = dll.update(truth + jitter());
        if (i > 500) {
            maxErr = std::max(maxErr, std::abs(filtered - truth));
            sumSq += (filtered - truth) * (filtered - truth);
            ++n;
        }
    }
    // Input: uniform +-1 ms (RMS 0.58 ms).
    CHECK(std::sqrt(sumSq / n) < 0.0002);
    CHECK(maxErr < 0.0006);
    CHECK(dll.period() == Approx(period).epsilon(1e-3));
}

TEST_CASE("TimeDll: irregular update spacing", "[clock][dll]") {
    TimeDll dll(1.0);
    dll.reset(0.0, 0.001);
    double t = 0.0;
    for (int i = 0; i < 3000; ++i) {
        const double units = 1 + (i % 3);
        t += units * 0.001002;
        dll.update(t, units);
    }
    CHECK(dll.period() == Approx(0.001002).epsilon(1e-5));
}

TEST_CASE("TimeDll: maxError clamps outliers", "[clock][dll]") {
    TimeDll dll(1.0);
    dll.reset(0.0, 0.01);
    for (int i = 1; i < 100; ++i) dll.update(i * 0.01);
    const double before = dll.period();
    dll.update(100 * 0.01 + 0.5, 1.0, 0.002);  // a 500 ms outlier
    CHECK(std::abs(dll.lastError() - 0.5) < 1e-9);
    CHECK(std::abs(dll.period() - before) < 0.0001);
}
