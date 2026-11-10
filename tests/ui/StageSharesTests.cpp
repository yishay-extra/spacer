#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <vector>

#include "core/LayoutPresets.h"
#include "core/spatial/Dbap.h"
#include "core/spatial/Vbap.h"
#include "ui/logic/StageShares.h"

using namespace spacer;
using Catch::Matchers::WithinAbs;

namespace {
std::size_t lfeIndex(const Layout& l) {
    for (std::size_t i = 0; i < l.speakers.size(); ++i)
        if (l.speakers[i].lfe) return i;
    FAIL("layout has no LFE");
    return 0;
}
}  // namespace

TEST_CASE("Shares equal the engine panners' gains", "[ui][shares]") {
    const Layout dome = *layoutpresets::load("dome22");
    StageShares shares;
    shares.prepare(dome);
    REQUIRE(shares.numSpeakers() == 22);
    const Position pos{37.0f, 22.0f, 1.5f};
    SourceSpec s;
    s.spread = 0.3f;
    std::vector<float> got(22), want(22);

    s.spatializer = SpatializerKind::Vbap;
    Vbap vbap;
    vbap.prepare(dome, 48000.0, 64);
    vbap.gainsFor(pos, 0.3f, want);
    REQUIRE(shares.gainsFor(s, pos, got));
    REQUIRE(got == want);

    s.spatializer = SpatializerKind::Dbap;
    Dbap dbap;
    dbap.prepare(dome, 48000.0, 64);
    dbap.gainsFor(pos, 0.3f, want);
    REQUIRE(shares.gainsFor(s, pos, got));
    REQUIRE(got == want);
}

TEST_CASE("Direct shares are the listed speakers, LFE included", "[ui][shares]") {
    const Layout l = *layoutpresets::load("5.1");
    const std::size_t lfe = lfeIndex(l);
    StageShares shares;
    shares.prepare(l);
    SourceSpec s;
    s.spatializer = SpatializerKind::Direct;
    s.directSpeakers = {l.speakers[0].id, l.speakers[lfe].id, 999};  // 999 is not in the layout
    std::vector<float> got(l.speakers.size(), 7.0f);
    REQUIRE(shares.gainsFor(s, {}, got));
    for (std::size_t i = 0; i < got.size(); ++i) REQUIRE(got[i] == ((i == 0 || i == lfe) ? 1.0f : 0.0f));
    s.directSpeakers.clear();
    REQUIRE(shares.gainsFor(s, {}, got));
    for (float g : got) REQUIRE(g == 0.0f);
}

TEST_CASE("VBAP and DBAP never share to the LFE speaker", "[ui][shares]") {
    const Layout l = *layoutpresets::load("5.1");
    StageShares shares;
    shares.prepare(l);
    std::vector<float> got(l.speakers.size());
    for (auto kind : {SpatializerKind::Vbap, SpatializerKind::Dbap}) {
        SourceSpec s;
        s.spatializer = kind;
        REQUIRE(shares.gainsFor(s, {0.0f, 0.0f, 2.0f}, got));
        REQUIRE(got[lfeIndex(l)] == 0.0f);
    }
}

TEST_CASE("Ambisonic sources and unprepared shares report no gains", "[ui][shares]") {
    StageShares shares;
    std::vector<float> out(4, 7.0f);
    SourceSpec s;
    REQUIRE(shares.numSpeakers() == 0);
    REQUIRE_FALSE(shares.gainsFor(s, {}, out));  // not prepared
    for (float g : out) REQUIRE(g == 0.0f);
    shares.prepare(*layoutpresets::load("quad"));
    s.spatializer = SpatializerKind::Ambisonic;
    out.assign(4, 7.0f);
    REQUIRE_FALSE(shares.gainsFor(s, {}, out));
    for (float g : out) REQUIRE(g == 0.0f);
    std::vector<float> wrong(3, 7.0f);
    s.spatializer = SpatializerKind::Vbap;
    REQUIRE_FALSE(shares.gainsFor(s, {}, wrong));  // size mismatch
}

TEST_CASE("Preparing again follows the new layout", "[ui][shares]") {
    StageShares shares;
    shares.prepare(*layoutpresets::load("quad"));
    REQUIRE(shares.numSpeakers() == 4);
    shares.prepare(*layoutpresets::load("dome22"));
    REQUIRE(shares.numSpeakers() == 22);
    std::vector<float> got(22);
    REQUIRE(shares.gainsFor(SourceSpec{}, {10.0f, 40.0f, 2.0f}, got));
    double sumSq = 0.0;
    for (float g : got) sumSq += static_cast<double>(g) * g;
    REQUIRE_THAT(sumSq, WithinAbs(1.0, 1e-3));
}

TEST_CASE("Shares are finite at the listener and on a speaker", "[ui][shares]") {
    const Layout dome = *layoutpresets::load("dome22");
    StageShares shares;
    shares.prepare(dome);
    std::vector<float> got(22);
    for (auto kind : {SpatializerKind::Vbap, SpatializerKind::Dbap})
        for (const Position& pos : {Position{0.0f, 0.0f, 0.0f}, dome.speakers[9].position}) {
            SourceSpec s;
            s.spatializer = kind;
            REQUIRE(shares.gainsFor(s, pos, got));
            for (float g : got) REQUIRE(std::isfinite(g));
        }
}
