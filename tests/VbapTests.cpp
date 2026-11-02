#include <atomic>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdlib>
#include <new>
#include <numeric>
#include <vector>

#include "core/LayoutPresets.h"
#include "core/spatial/Vbap.h"

// --- allocation counter hook (active only while `g_countAllocs` is set) -----
namespace {
std::atomic<bool> g_countAllocs{false};
std::atomic<long> g_allocs{0};
}  // namespace

void* operator new(std::size_t n) {
    if (g_countAllocs.load(std::memory_order_relaxed)) g_allocs.fetch_add(1, std::memory_order_relaxed);
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) {
    return operator new(n);
}
void operator delete(void* p) noexcept {
    std::free(p);
}
void operator delete[](void* p) noexcept {
    std::free(p);
}
void operator delete(void* p, std::size_t) noexcept {
    std::free(p);
}
void operator delete[](void* p, std::size_t) noexcept {
    std::free(p);
}

using Catch::Approx;
using namespace spacer;

namespace {

Layout preset(const std::string& id) {
    auto l = layoutpresets::load(id);
    REQUIRE(l.has_value());
    return *l;
}

std::vector<float> gains(const Vbap& v, const Layout& l, float az, float spread = 0.0f, float el = 0.0f) {
    std::vector<float> g(l.speakers.size());
    v.gainsFor({az, el, 2.0f}, spread, g);
    return g;
}

float sumSq(const std::vector<float>& g) {
    return std::accumulate(g.begin(), g.end(), 0.0f, [](float a, float x) { return a + x * x; });
}

const char* kPanned2D[] = {"stereo", "quad", "5.1", "7.1", "octagon", "ring12", "ring16"};

}  // namespace

TEST_CASE("VBAP: source at a speaker gives gain 1 there and 0 elsewhere") {
    for (const char* id : kPanned2D) {
        const Layout l = preset(id);
        Vbap v;
        v.prepare(l, 48000.0, 256);
        for (std::size_t i = 0; i < l.speakers.size(); ++i) {
            if (l.speakers[i].lfe) continue;
            const auto g = gains(v, l, l.speakers[i].position.azimuth);
            for (std::size_t j = 0; j < g.size(); ++j)
                CHECK(g[j] == Approx(i == j ? 1.0f : 0.0f).margin(1e-4));
        }
    }
}

TEST_CASE("VBAP: midway between adjacent speakers gives 1/sqrt(2) each") {
    for (const char* id : {"quad", "octagon", "ring12", "ring16"}) {
        const Layout l = preset(id);
        Vbap v;
        v.prepare(l, 48000.0, 256);
        const std::size_t n = l.speakers.size();
        for (std::size_t i = 0; i < n; ++i) {
            const float a = l.speakers[i].position.azimuth;
            const float b = l.speakers[(i + 1) % n].position.azimuth;
            const float mid = Position::wrapAzimuth(a + Position::wrapAzimuth(b - a) * 0.5f);
            const auto g = gains(v, l, mid);
            CHECK(g[i] == Approx(std::sqrt(0.5f)).margin(1e-4));
            CHECK(g[(i + 1) % n] == Approx(std::sqrt(0.5f)).margin(1e-4));
        }
    }
}

TEST_CASE("VBAP: energy is constant (sum g^2 = 1) at 360 points, with and without spread") {
    for (const char* id : kPanned2D) {
        const Layout l = preset(id);
        Vbap v;
        v.prepare(l, 48000.0, 256);
        for (float spread : {0.0f, 0.3f, 1.0f})
            for (int deg = -180; deg < 180; ++deg) {
                const auto g = gains(v, l, static_cast<float>(deg) + 0.37f, spread);
                INFO(id << " az=" << deg << " spread=" << spread);
                REQUIRE(sumSq(g) == Approx(1.0f).margin(1e-4));
                for (float x : g) REQUIRE(x >= 0.0f);
            }
    }
}

TEST_CASE("VBAP: elevation of the source is ignored in 2D mode") {
    const Layout l = preset("octagon");
    Vbap v;
    v.prepare(l, 48000.0, 256);
    CHECK(gains(v, l, 17.0f, 0.0f, 0.0f) == gains(v, l, 17.0f, 0.0f, 60.0f));
}

TEST_CASE("VBAP: stereo pair is continuous across the wide rear gap") {
    const Layout l = preset("stereo");
    Vbap v;
    v.prepare(l, 48000.0, 256);
    // Inside the pair: ordinary VBAP; behind: constant-power cross-fade.
    const auto rear = gains(v, l, 180.0f);
    CHECK(rear[0] == Approx(rear[1]).margin(1e-4));
    float prev0 = gains(v, l, -30.0f)[0];
    for (int deg = -31; deg > -330; --deg) {
        const float g0 = gains(v, l, static_cast<float>(deg))[0];
        REQUIRE(std::abs(g0 - prev0) < 0.05f);
        prev0 = g0;
    }
}

TEST_CASE("VBAP: spread widens the distribution and stays normalised") {
    const Layout l = preset("ring16");
    Vbap v;
    v.prepare(l, 48000.0, 256);
    const auto point = gains(v, l, 0.0f, 0.0f);
    const auto wide = gains(v, l, 0.0f, 0.5f);
    CHECK(point[0] == Approx(1.0f).margin(1e-4));
    CHECK(wide[0] < point[0]);
    CHECK(wide[1] > 0.0f);
    CHECK(wide[15] > 0.0f);
    CHECK(sumSq(wide) == Approx(1.0f).margin(1e-4));
}

TEST_CASE("VBAP: LFE speakers are ignored (5.1 preset)") {
    const Layout l = preset("5.1");
    std::size_t lfe = l.speakers.size();
    for (std::size_t i = 0; i < l.speakers.size(); ++i)
        if (l.speakers[i].lfe) lfe = i;
    REQUIRE(lfe < l.speakers.size());

    Vbap v;
    v.prepare(l, 48000.0, 256);
    CHECK(v.is2D());
    CHECK(v.numPannedSpeakers() == 5);

    // LFE shares C's direction: panning to C must give C = 1 and LFE = 0.
    const auto atC = gains(v, l, 0.0f);
    for (std::size_t i = 0; i < atC.size(); ++i)
        CHECK(atC[i] == Approx(l.speakers[i].name == "C" ? 1.0f : 0.0f).margin(1e-4));

    // LFE silent everywhere, with and without spread; others identical to a
    // layout with the LFE removed.
    Layout five = l;
    five.speakers.erase(five.speakers.begin() + static_cast<long>(lfe));
    Vbap v5;
    v5.prepare(five, 48000.0, 256);
    for (float spread : {0.0f, 0.6f})
        for (int deg = -180; deg < 180; ++deg) {
            const auto g = gains(v, l, static_cast<float>(deg), spread);
            const auto g5 = gains(v5, five, static_cast<float>(deg), spread);
            REQUIRE(g[lfe] == 0.0f);
            REQUIRE(sumSq(g) == Approx(1.0f).margin(1e-4));
            std::size_t j = 0;
            for (std::size_t i = 0; i < g.size(); ++i)
                if (i != lfe) REQUIRE(g[i] == Approx(g5[j++]).margin(1e-6));
        }

    // render() leaves the LFE output buffer silent.
    const int n = 64;
    std::vector<std::vector<float>> out(l.speakers.size(), std::vector<float>(n, 0.0f));
    std::vector<float*> ptrs;
    for (auto& o : out) ptrs.push_back(o.data());
    SpeakerBus bus;
    bus.speakers = ptrs.data();
    bus.numSpeakers = static_cast<int>(ptrs.size());
    bus.numSamples = n;
    std::vector<float> in(n, 1.0f);
    v.render(in.data(), n, {0.0f, 0.0f, 2.0f}, {90.0f, 0.0f, 2.0f}, 0.4f, bus);
    for (float s : out[lfe]) CHECK(s == 0.0f);
}

TEST_CASE("VBAP: 3D layouts are flagged") {
    Vbap v;
    v.prepare(preset("7.1.4"), 48000.0, 256);
    CHECK_FALSE(v.is2D());
}

TEST_CASE("VBAP: render of a constant signal matches gainsFor") {
    const Layout l = preset("octagon");
    Vbap v;
    v.prepare(l, 48000.0, 128);
    const int n = 128;
    std::vector<std::vector<float>> out(l.speakers.size(), std::vector<float>(n, 0.0f));
    std::vector<float*> ptrs;
    for (auto& o : out) ptrs.push_back(o.data());
    SpeakerBus bus;
    bus.speakers = ptrs.data();
    bus.numSpeakers = static_cast<int>(ptrs.size());
    bus.numSamples = n;
    std::vector<float> in(n, 0.5f);

    SECTION("static position") {
        const Position p{23.0f, 0.0f, 2.0f};
        v.render(in.data(), n, p, p, 0.2f, bus);
        const auto g = gains(v, l, 23.0f, 0.2f);
        for (std::size_t s = 0; s < out.size(); ++s)
            for (float x : out[s]) REQUIRE(x == Approx(0.5f * g[s]).margin(1e-6));
    }
    SECTION("moving position interpolates linearly per sample") {
        const Position a{10.0f, 0.0f, 2.0f}, b{30.0f, 0.0f, 2.0f};
        v.render(in.data(), n, a, b, 0.0f, bus);
        const auto g0 = gains(v, l, 10.0f), g1 = gains(v, l, 30.0f);
        for (std::size_t s = 0; s < out.size(); ++s)
            for (int i = 0; i < n; ++i) {
                const float t = static_cast<float>(i) / static_cast<float>(n);
                REQUIRE(out[s][static_cast<std::size_t>(i)] ==
                        Approx(0.5f * (g0[s] + (g1[s] - g0[s]) * t)).margin(1e-5));
            }
    }
}

TEST_CASE("VBAP: render does not allocate") {
    const Layout l = preset("5.1");
    Vbap v;
    v.prepare(l, 48000.0, 256);
    const int n = 256;
    std::vector<std::vector<float>> out(l.speakers.size(), std::vector<float>(n, 0.0f));
    std::vector<float*> ptrs;
    for (auto& o : out) ptrs.push_back(o.data());
    SpeakerBus bus;
    bus.speakers = ptrs.data();
    bus.numSpeakers = static_cast<int>(ptrs.size());
    bus.numSamples = n;
    std::vector<float> in(n, 1.0f);

    g_allocs = 0;
    g_countAllocs = true;
    for (int i = 0; i < 100; ++i)
        v.render(in.data(), n, {static_cast<float>(i), 0.0f, 2.0f}, {static_cast<float>(i + 7), 0.0f, 2.0f},
                 i % 2 ? 0.5f : 0.0f, bus);
    g_countAllocs = false;
    CHECK(g_allocs.load() == 0);
}
