#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

#include "core/LayoutPresets.h"
#include "core/spatial/Dbap.h"

using namespace spacer;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

Layout makeLayout(const std::vector<Position>& positions, const std::vector<bool>& lfe = {}) {
    Layout layout;
    layout.name = "test";
    for (std::size_t i = 0; i < positions.size(); ++i) {
        Speaker s;
        s.id = static_cast<int>(i) + 1;
        s.name = "S" + std::to_string(i + 1);
        s.position = positions[i];
        s.outputChannel = static_cast<int>(i);
        s.lfe = i < lfe.size() && lfe[i];
        layout.speakers.push_back(s);
    }
    return layout;
}

std::vector<float> gainsOf(const Dbap& dbap, const Position& pos, float spread, std::size_t count) {
    std::vector<float> gains(count, -1.0f);
    dbap.gainsFor(pos, spread, gains);
    return gains;
}

// Σ g² over the panned (non-LFE) speakers.
double pannedPower(const Layout& layout, const std::vector<float>& gains) {
    double sum = 0.0;
    for (std::size_t i = 0; i < layout.speakers.size(); ++i) {
        if (layout.speakers[i].lfe) continue;
        sum += static_cast<double>(gains[i]) * static_cast<double>(gains[i]);
    }
    return sum;
}

// Shannon entropy of the normalised power distribution g²/Σg².
double powerEntropy(const std::vector<float>& gains) {
    double total = 0.0;
    for (float g : gains) total += static_cast<double>(g) * static_cast<double>(g);
    double h = 0.0;
    for (float g : gains) {
        const double p = static_cast<double>(g) * static_cast<double>(g) / total;
        if (p > 0.0) h -= p * std::log(p);
    }
    return h;
}

}  // namespace

TEST_CASE("DBAP: spread maps to spatial blur r_s = 0.2 + 3·spread (SPEC §6.2)") {
    REQUIRE_THAT(Dbap::blurFor(0.0f), WithinAbs(0.2f, 1e-6f));
    REQUIRE_THAT(Dbap::blurFor(0.5f), WithinAbs(1.7f, 1e-6f));
    REQUIRE_THAT(Dbap::blurFor(1.0f), WithinAbs(3.2f, 1e-6f));
    REQUIRE_THAT(Dbap::blurFor(2.0f), WithinAbs(3.2f, 1e-6f));  // clamped
}

TEST_CASE("DBAP: hand-computed gains for two speakers, source on one of them") {
    // Speakers at (x=+1, y=0) and (x=-1, y=0); source at (+1, 0), spread 0 → r_s = 0.2.
    // d_A = 0 + 0.2 = 0.2, d_B = 2 + 0.2 = 2.2.
    // g = d^(-a/2) with a = 6 → g_B / g_A = (0.2 / 2.2)^3 = 1/1331.
    // Normalising Σg² = 1 gives g_A = 1 / sqrt(1 + 1331^-2).
    const Layout layout = makeLayout({Position{90, 0, 1}, Position{-90, 0, 1}});
    Dbap dbap;
    dbap.prepare(layout, 48000.0, 512);

    const auto gains = gainsOf(dbap, Position{90, 0, 1}, 0.0f, 2);
    const double gA = 1.0 / std::sqrt(1.0 + 1.0 / (1331.0 * 1331.0));
    REQUIRE_THAT(gains[0], WithinAbs(static_cast<float>(gA), 1e-6f));
    REQUIRE_THAT(gains[1] / gains[0], WithinRel(1.0f / 1331.0f, 1e-4f));
    REQUIRE_THAT(pannedPower(layout, gains), WithinAbs(1.0, 1e-6));
}

TEST_CASE("DBAP: rolloff 0 gives equal gains at any position (hand-computed 1/√4 each)") {
    const Layout layout =
        makeLayout({Position{45, 0, 2}, Position{135, 0, 2}, Position{-135, 0, 2}, Position{-45, 0, 2}});
    Dbap dbap(0.0f);
    dbap.prepare(layout, 48000.0, 512);

    const auto gains = gainsOf(dbap, Position{10, 0, 1}, 0.7f, 4);
    for (float g : gains) REQUIRE_THAT(g, WithinAbs(0.5f, 1e-6f));
}

TEST_CASE("DBAP: Σg² = 1 over a grid of directions, distances and spreads (7.1.4)") {
    const auto loaded = layoutpresets::load("7.1.4");
    REQUIRE(loaded.has_value());
    Dbap dbap;
    dbap.prepare(*loaded, 48000.0, 512);

    const std::size_t n = loaded->speakers.size();
    for (float az = -180.0f; az <= 170.0f; az += 20.0f)
        for (float el : {0.0f, 30.0f, 60.0f, 90.0f})
            for (float dist : {0.0f, 0.5f, 3.0f})
                for (float spread : {0.0f, 0.5f, 1.0f}) {
                    INFO("az=" << az << " el=" << el << " dist=" << dist << " spread=" << spread);
                    const auto gains = gainsOf(dbap, Position{az, el, dist}, spread, n);
                    REQUIRE_THAT(pannedPower(*loaded, gains), WithinAbs(1.0, 1e-5));
                }
}

TEST_CASE("DBAP: spread widens the gain distribution (entropy rises monotonically)") {
    const auto octagon = layoutpresets::load("octagon");
    REQUIRE(octagon.has_value());
    Dbap dbap;
    dbap.prepare(*octagon, 48000.0, 512);

    for (float az : {30.0f, 100.0f}) {
        INFO("source azimuth " << az);
        const Position pos{az, 0.0f, 1.5f};
        double previous = -1.0;
        for (int step = 0; step <= 10; ++step) {
            const float spread = static_cast<float>(step) / 10.0f;
            INFO("spread " << spread);
            const double h = powerEntropy(gainsOf(dbap, pos, spread, octagon->speakers.size()));
            REQUIRE(h > previous);
            previous = h;
        }
    }
}

TEST_CASE("DBAP: LFE speakers are never panned (SPEC §7, 5.1 preset)") {
    const auto withLfe = layoutpresets::load("5.1");
    REQUIRE(withLfe.has_value());

    std::size_t lfeIndex = withLfe->speakers.size();
    Layout withoutLfe = *withLfe;
    withoutLfe.speakers.clear();
    for (std::size_t i = 0; i < withLfe->speakers.size(); ++i) {
        if (withLfe->speakers[i].lfe) {
            REQUIRE(lfeIndex == withLfe->speakers.size());  // exactly one LFE in 5.1
            lfeIndex = i;
        } else {
            withoutLfe.speakers.push_back(withLfe->speakers[i]);
        }
    }
    REQUIRE(lfeIndex < withLfe->speakers.size());

    Dbap with;
    with.prepare(*withLfe, 48000.0, 512);
    Dbap without;
    without.prepare(withoutLfe, 48000.0, 512);

    // The LFE sits in the direction of C (az 0), so this includes a source on the LFE itself.
    const Position positions[] = {Position{0, 0, 2}, Position{-30, 0, 2}, Position{45, 0, 1},
                                  Position{170, 0, 3}, Position{-110, 0, 0.5f}};
    for (const auto& pos : positions)
        for (float spread : {0.0f, 0.5f, 1.0f}) {
            INFO("az=" << pos.azimuth << " dist=" << pos.distance << " spread=" << spread);
            const auto gains = gainsOf(with, pos, spread, withLfe->speakers.size());
            const auto reference = gainsOf(without, pos, spread, withoutLfe.speakers.size());

            REQUIRE(gains[lfeIndex] == 0.0f);
            std::size_t k = 0;
            for (std::size_t i = 0; i < gains.size(); ++i) {
                if (i == lfeIndex) continue;
                REQUIRE_THAT(gains[i], WithinAbs(reference[k], 1e-6f));
                ++k;
            }
            REQUIRE_THAT(pannedPower(*withLfe, gains), WithinAbs(1.0, 1e-5));
        }
}

TEST_CASE("DBAP: render never writes to LFE output and matches gainsFor on other speakers") {
    const auto layout = layoutpresets::load("5.1");
    REQUIRE(layout.has_value());
    Dbap dbap;
    dbap.prepare(*layout, 48000.0, 64);

    constexpr int kN = 64;
    const std::size_t n = layout->speakers.size();
    std::vector<float> mono(kN, 1.0f);
    std::vector<std::vector<float>> buffers(n, std::vector<float>(kN, 0.0f));
    std::vector<float*> pointers;
    for (auto& b : buffers) pointers.push_back(b.data());

    SpeakerBus bus{};
    bus.speakers = pointers.data();
    bus.numSpeakers = static_cast<int>(n);
    bus.numSamples = kN;

    const Position pos{-30, 0, 2};
    dbap.render(mono.data(), kN, pos, pos, 0.5f, bus);
    const auto gains = gainsOf(dbap, pos, 0.5f, n);

    for (std::size_t i = 0; i < n; ++i) {
        INFO("speaker " << layout->speakers[i].name);
        for (int s = 0; s < kN; ++s) {
            if (layout->speakers[i].lfe) {
                REQUIRE(buffers[i][static_cast<std::size_t>(s)] == 0.0f);
            } else {
                REQUIRE_THAT(buffers[i][static_cast<std::size_t>(s)], WithinAbs(gains[i], 1e-6f));
            }
        }
    }
}

TEST_CASE("DBAP: render ramps gains linearly from start to end over the block") {
    const auto layout = layoutpresets::load("5.1");
    REQUIRE(layout.has_value());
    Dbap dbap;
    dbap.prepare(*layout, 48000.0, 64);

    constexpr int kN = 4;
    const std::size_t n = layout->speakers.size();
    std::vector<float> mono(kN, 1.0f);
    std::vector<std::vector<float>> buffers(n, std::vector<float>(kN, 0.0f));
    std::vector<float*> pointers;
    for (auto& b : buffers) pointers.push_back(b.data());
    SpeakerBus bus{};
    bus.speakers = pointers.data();
    bus.numSpeakers = static_cast<int>(n);
    bus.numSamples = kN;

    const Position start{-30, 0, 2};  // on L
    const Position end{30, 0, 2};     // on R
    dbap.render(mono.data(), kN, start, end, 0.0f, bus);
    const auto g0 = gainsOf(dbap, start, 0.0f, n);
    const auto g1 = gainsOf(dbap, end, 0.0f, n);

    for (std::size_t i = 0; i < n; ++i) {
        if (layout->speakers[i].lfe) continue;
        for (int s = 0; s < kN; ++s) {
            const float expected =
                g0[i] + (g1[i] - g0[i]) * static_cast<float>(s + 1) / static_cast<float>(kN);
            INFO("speaker " << layout->speakers[i].name << " sample " << s);
            REQUIRE_THAT(buffers[i][static_cast<std::size_t>(s)], WithinAbs(expected, 1e-6f));
        }
    }
}

TEST_CASE("DBAP: a layout with only LFE speakers produces silence, not NaN") {
    const Layout layout = makeLayout({Position{0, 0, 1}, Position{90, 0, 1}}, {true, true});
    Dbap dbap;
    dbap.prepare(layout, 48000.0, 512);

    const auto gains = gainsOf(dbap, Position{20, 10, 1}, 0.5f, 2);
    for (float g : gains) REQUIRE(g == 0.0f);
}
