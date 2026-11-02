#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <numeric>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "core/LayoutPresets.h"
#include "core/spatial/SphereHull.h"
#include "core/spatial/Vbap.h"

// 3D VBAP (SPEC §6.1, WP2b). The allocation check for render() lives in
// VbapTests.cpp (hook in AllocCounter.cpp).

using Catch::Approx;
using namespace spacer;

namespace {

Layout preset(const std::string& id) {
    auto l = layoutpresets::load(id);
    REQUIRE(l.has_value());
    return *l;
}

Speaker spk(int id, float az, float el, bool lfe = false) {
    Speaker s;
    s.id = id;
    s.name = "S" + std::to_string(id);
    s.position = {az, el, 2.0f};
    s.outputChannel = id - 1;
    s.lfe = lfe;
    return s;
}

/// 8 at 0°, 4 at 45°, zenith: nothing below the horizon.
Layout hemisphere() {
    Layout l;
    l.name = "hemisphere";
    int id = 1;
    for (int k = 0; k < 8; ++k)
        l.speakers.push_back(spk(id++, Position::wrapAzimuth(45.0f * static_cast<float>(k)), 0.0f));
    for (int k = 0; k < 4; ++k)
        l.speakers.push_back(spk(id++, Position::wrapAzimuth(45.0f + 90.0f * static_cast<float>(k)), 45.0f));
    l.speakers.push_back(spk(id++, 0.0f, 90.0f));
    return l;
}

/// 6 at 0°, 4 at -40°: nothing above the horizon (needs a ceiling).
Layout lowerHemisphere() {
    Layout l;
    l.name = "lower";
    int id = 1;
    for (int k = 0; k < 6; ++k)
        l.speakers.push_back(spk(id++, Position::wrapAzimuth(60.0f * static_cast<float>(k)), 0.0f));
    for (int k = 0; k < 4; ++k)
        l.speakers.push_back(spk(id++, Position::wrapAzimuth(90.0f * static_cast<float>(k)), -40.0f));
    return l;
}

std::vector<float> gains(const Vbap& v, const Layout& l, float az, float el, float spread = 0.0f) {
    std::vector<float> g(l.speakers.size());
    v.gainsFor({az, el, 2.0f}, spread, g);
    return g;
}

std::vector<float> gainsDir(const Vbap& v, const Layout& l, const Cartesian& d, float spread = 0.0f) {
    std::vector<float> g(l.speakers.size());
    Position p = Position::fromCartesian(d);
    p.distance = 2.0f;
    v.gainsFor(p, spread, g);
    return g;
}

float sumSq(const std::vector<float>& g) {
    return std::accumulate(g.begin(), g.end(), 0.0f, [](float a, float x) { return a + x * x; });
}

int panned(const Layout& l) {
    int n = 0;
    for (const auto& s : l.speakers) n += s.lfe ? 0 : 1;
    return n;
}

}  // namespace

TEST_CASE("SphereHull: octahedron gives 8 triangles enclosing the origin") {
    const std::vector<Cartesian> pts{{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    const SphereHull h = triangulateSphere(pts);
    CHECK_FALSE(h.degenerate);
    CHECK(h.triangles.size() == 8);
    CHECK(h.minFaceDistance == Approx(1.0 / std::sqrt(3.0)));
}

TEST_CASE("SphereHull: coplanar points are degenerate") {
    const std::vector<Cartesian> pts{{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}};
    CHECK(triangulateSphere(pts).degenerate);
}

TEST_CASE("SphereHull: a coplanar face is split into a fan from its lowest index") {
    // Cube: every face is a square, split into two triangles from its lowest vertex.
    std::vector<Cartesian> pts;
    const float c = 1.0f / std::sqrt(3.0f);
    for (int i = 0; i < 8; ++i) pts.push_back({i & 1 ? c : -c, i & 2 ? c : -c, i & 4 ? c : -c});
    const SphereHull h = triangulateSphere(pts);
    REQUIRE(h.triangles.size() == 12);  // 2·8 − 4
    for (const auto& t : h.triangles) CHECK(t[0] < std::min(t[1], t[2]));
}

TEST_CASE("VBAP 3D: 7.1.4 and dome22 triangulate with an imaginary floor") {
    for (const char* id : {"7.1.4", "dome22"}) {
        const Layout l = preset(id);
        Vbap v;
        v.prepare(l, 48000.0, 256);
        INFO(id);
        REQUIRE_FALSE(v.is2D());
        CHECK(v.numPannedSpeakers() == panned(l));
        CHECK(v.numImaginarySpeakers() == 1);
        // A closed triangulated sphere with V vertices has 2V − 4 triangles.
        const int vertices = v.numPannedSpeakers() + v.numImaginarySpeakers();
        CHECK(v.numTriangles() == 2 * vertices - 4);
        // Every panned speaker is a vertex; the LFE is not.
        std::set<int> used;
        for (int t = 0; t < v.numTriangles(); ++t)
            for (int s : v.triangle(t)) used.insert(s);
        for (std::size_t i = 0; i < l.speakers.size(); ++i)
            CHECK(used.count(static_cast<int>(i)) == (l.speakers[i].lfe ? 0u : 1u));
        CHECK(used.count(-1) == 1);
    }
}

TEST_CASE("VBAP 3D: source at a speaker gives gain 1 there and 0 elsewhere") {
    for (const Layout& l : {preset("7.1.4"), preset("dome22"), hemisphere(), lowerHemisphere()}) {
        Vbap v;
        v.prepare(l, 48000.0, 256);
        REQUIRE_FALSE(v.is2D());
        for (std::size_t i = 0; i < l.speakers.size(); ++i) {
            if (l.speakers[i].lfe) continue;
            const auto& p = l.speakers[i].position;
            const auto g = gains(v, l, p.azimuth, p.elevation);
            INFO(l.name << " speaker " << l.speakers[i].name);
            for (std::size_t j = 0; j < g.size(); ++j)
                CHECK(g[j] == Approx(i == j ? 1.0f : 0.0f).margin(1e-4));
        }
    }
}

TEST_CASE("VBAP 3D: source on an edge between two speakers gives two non-zero gains") {
    for (const Layout& l : {preset("7.1.4"), preset("dome22"), hemisphere()}) {
        Vbap v;
        v.prepare(l, 48000.0, 256);
        std::set<std::pair<int, int>> edges;
        for (int t = 0; t < v.numTriangles(); ++t) {
            const auto tri = v.triangle(t);
            for (int j = 0; j < 3; ++j) {
                const int a = tri[static_cast<std::size_t>(j)],
                          b = tri[static_cast<std::size_t>((j + 1) % 3)];
                if (a >= 0 && b >= 0) edges.insert({std::min(a, b), std::max(a, b)});
            }
        }
        REQUIRE(edges.size() > 10);
        for (const auto& [a, b] : edges) {
            const Cartesian da = l.speakers[static_cast<std::size_t>(a)].position.direction();
            const Cartesian db = l.speakers[static_cast<std::size_t>(b)].position.direction();
            const auto g = gainsDir(v, l, (da + db).normalised());
            INFO(l.name << " edge " << l.speakers[static_cast<std::size_t>(a)].name << "-"
                        << l.speakers[static_cast<std::size_t>(b)].name);
            int nonZero = 0;
            for (float x : g) nonZero += x > 1e-3f ? 1 : 0;
            CHECK(nonZero == 2);
            CHECK(g[static_cast<std::size_t>(a)] == Approx(std::sqrt(0.5f)).margin(1e-3));
            CHECK(g[static_cast<std::size_t>(b)] == Approx(std::sqrt(0.5f)).margin(1e-3));
        }
    }
}

TEST_CASE("VBAP 3D: energy is constant (sum g^2 = 1) over a 1 degree grid of the sphere") {
    for (const Layout& l : {preset("7.1.4"), preset("dome22"), hemisphere(), lowerHemisphere()}) {
        Vbap v;
        v.prepare(l, 48000.0, 256);
        for (float spread : {0.0f, 0.4f, 1.0f})
            for (int el = -90; el <= 90; ++el)
                for (int az = -179; az <= 180; ++az) {
                    const auto g = gains(v, l, static_cast<float>(az), static_cast<float>(el), spread);
                    INFO(l.name << " az=" << az << " el=" << el << " spread=" << spread);
                    REQUIRE(sumSq(g) == Approx(1.0f).margin(1e-4));
                    for (float x : g) REQUIRE(x >= 0.0f);
                }
    }
}

TEST_CASE("VBAP 3D: a hemisphere layout works via the imaginary floor") {
    const Layout l = hemisphere();
    Vbap v;
    v.prepare(l, 48000.0, 256);
    REQUIRE_FALSE(v.is2D());
    CHECK(v.numImaginarySpeakers() == 1);

    // Below the horizon only the horizontal ring sounds, pulled to the ring
    // pair under the source (the floor's gain is discarded).
    for (int el : {-10, -45, -80}) {
        const auto g = gains(v, l, 22.5f, static_cast<float>(el));
        INFO("el=" << el);
        CHECK(g[0] == Approx(std::sqrt(0.5f)).margin(1e-4));
        CHECK(g[1] == Approx(std::sqrt(0.5f)).margin(1e-4));
        CHECK(sumSq(g) == Approx(1.0f).margin(1e-4));
    }
    const auto atSpeakerAz = gains(v, l, 90.0f, -60.0f);
    CHECK(atSpeakerAz[2] == Approx(1.0f).margin(1e-4));

    // Straight down every ring speaker shares the floor equally.
    const auto down = gains(v, l, 0.0f, -90.0f);
    for (std::size_t i = 0; i < l.speakers.size(); ++i)
        CHECK(down[i] == Approx(i < 8 ? std::sqrt(1.0f / 8.0f) : 0.0f).margin(1e-4));
}

TEST_CASE("VBAP 3D: a layout with nothing above gets an imaginary ceiling") {
    const Layout l = lowerHemisphere();
    Vbap v;
    v.prepare(l, 48000.0, 256);
    REQUIRE_FALSE(v.is2D());
    CHECK(v.numImaginarySpeakers() == 1);
    bool hasCeilingVertex = false;
    for (int t = 0; t < v.numTriangles(); ++t)
        for (int s : v.triangle(t)) hasCeilingVertex |= s < 0;
    CHECK(hasCeilingVertex);
    const auto up = gains(v, l, 0.0f, 90.0f);
    for (std::size_t i = 0; i < l.speakers.size(); ++i)
        CHECK(up[i] == Approx(i < 6 ? std::sqrt(1.0f / 6.0f) : 0.0f).margin(1e-4));
}

TEST_CASE("VBAP 3D: a full sphere needs no imaginary speaker") {
    Layout l;
    int id = 1;
    for (auto [az, el] : std::vector<std::pair<float, float>>{
             {0, 0}, {90, 0}, {180, 0}, {-90, 0}, {0, 90}, {0, -90}})  // octahedron
        l.speakers.push_back(spk(id++, az, el));
    Vbap v;
    v.prepare(l, 48000.0, 256);
    REQUIRE_FALSE(v.is2D());
    CHECK(v.numImaginarySpeakers() == 0);
    CHECK(v.numTriangles() == 8);
    // Centre of a face: equal gains on its three speakers.
    const auto g = gainsDir(v, l, Cartesian{1, 1, 1}.normalised());
    CHECK(g[0] == Approx(std::sqrt(1.0f / 3.0f)).margin(1e-4));  // y
    CHECK(g[1] == Approx(std::sqrt(1.0f / 3.0f)).margin(1e-4));  // x
    CHECK(g[4] == Approx(std::sqrt(1.0f / 3.0f)).margin(1e-4));  // z
}

TEST_CASE("VBAP 3D: LFE speakers are excluded (7.1.4 preset)") {
    const Layout l = preset("7.1.4");
    std::size_t lfe = l.speakers.size();
    for (std::size_t i = 0; i < l.speakers.size(); ++i)
        if (l.speakers[i].lfe) lfe = i;
    REQUIRE(lfe < l.speakers.size());

    Vbap v;
    v.prepare(l, 48000.0, 256);
    CHECK(v.numPannedSpeakers() == 11);

    // LFE shares C's direction: C gets 1, LFE 0.
    const auto atC = gains(v, l, 0.0f, 0.0f);
    for (std::size_t i = 0; i < atC.size(); ++i)
        CHECK(atC[i] == Approx(l.speakers[i].name == "C" ? 1.0f : 0.0f).margin(1e-4));

    Layout without = l;
    without.speakers.erase(without.speakers.begin() + static_cast<long>(lfe));
    Vbap v2;
    v2.prepare(without, 48000.0, 256);
    for (float spread : {0.0f, 0.5f})
        for (int el = -90; el <= 90; el += 5)
            for (int az = -180; az < 180; az += 5) {
                const auto g = gains(v, l, static_cast<float>(az), static_cast<float>(el), spread);
                const auto g2 = gains(v2, without, static_cast<float>(az), static_cast<float>(el), spread);
                REQUIRE(g[lfe] == 0.0f);
                std::size_t j = 0;
                for (std::size_t i = 0; i < g.size(); ++i)
                    if (i != lfe) REQUIRE(g[i] == Approx(g2[j++]).margin(1e-6));
            }
}

TEST_CASE("VBAP 3D: speakers sharing a direction use only the first") {
    Layout l = hemisphere();
    l.speakers.push_back(spk(99, 45.0f, 45.0f));  // duplicate of speaker index 8
    Vbap v;
    v.prepare(l, 48000.0, 256);
    CHECK(v.numPannedSpeakers() == 13);
    const auto g = gains(v, l, 45.0f, 45.0f);
    CHECK(g[8] == Approx(1.0f).margin(1e-4));
    CHECK(g.back() == 0.0f);
}

TEST_CASE("VBAP 3D: spread widens the distribution and stays normalised") {
    const Layout l = preset("dome22");
    Vbap v;
    v.prepare(l, 48000.0, 256);
    const std::size_t z = l.speakers.size() - 1;
    REQUIRE(l.speakers[z].name == "Z");
    const auto point = gains(v, l, 0.0f, 90.0f, 0.0f);
    const auto wide = gains(v, l, 0.0f, 90.0f, 0.5f);
    CHECK(point[z] == Approx(1.0f).margin(1e-4));
    CHECK(wide[z] < point[z]);
    int nonZero = 0;
    for (float x : wide) nonZero += x > 1e-3f ? 1 : 0;
    CHECK(nonZero >= 5);
    CHECK(sumSq(wide) == Approx(1.0f).margin(1e-4));

    // Any spread pulls energy off the speaker the source sits on.
    for (float sp : {0.25f, 0.75f, 1.0f}) {
        const auto g = gains(v, l, 0.0f, 90.0f, sp);
        CHECK(g[z] < 0.999f);
        CHECK(sumSq(g) == Approx(1.0f).margin(1e-4));
    }
}

TEST_CASE("VBAP 3D: deterministic") {
    for (const Layout& l : {preset("7.1.4"), preset("dome22"), hemisphere()}) {
        Vbap a, b;
        a.prepare(l, 48000.0, 256);
        b.prepare(l, 44100.0, 64);
        REQUIRE(a.numTriangles() == b.numTriangles());
        for (int t = 0; t < a.numTriangles(); ++t) CHECK(a.triangle(t) == b.triangle(t));
        for (int k = 0; k < 500; ++k) {
            const float az = static_cast<float>((k * 137) % 360) - 179.5f;
            const float el = static_cast<float>((k * 53) % 181) - 90.0f;
            const float s = static_cast<float>(k % 5) * 0.2f;
            REQUIRE(gains(a, l, az, el, s) == gains(b, l, az, el, s));
        }
        // Re-preparing the same instance yields the same result.
        const auto before = gains(a, l, 12.0f, 34.0f, 0.3f);
        a.prepare(preset("octagon"), 48000.0, 256);
        a.prepare(l, 48000.0, 256);
        CHECK(gains(a, l, 12.0f, 34.0f, 0.3f) == before);
    }
}

TEST_CASE("VBAP 3D: render of a constant signal matches gainsFor") {
    const Layout l = preset("7.1.4");
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

    const Position a{10.0f, 0.0f, 2.0f}, b{40.0f, 50.0f, 2.0f};
    v.render(in.data(), n, a, b, 0.2f, bus);
    const auto g0 = gains(v, l, 10.0f, 0.0f, 0.2f), g1 = gains(v, l, 40.0f, 50.0f, 0.2f);
    for (std::size_t s = 0; s < out.size(); ++s)
        for (int i = 0; i < n; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(n);
            REQUIRE(out[s][static_cast<std::size_t>(i)] ==
                    Approx(0.5f * (g0[s] + (g1[s] - g0[s]) * t)).margin(1e-5));
        }
}
