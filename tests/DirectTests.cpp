#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "core/LayoutPresets.h"
#include "core/spatial/Direct.h"

using namespace spacer;

namespace {

Layout preset(const std::string& id) {
    auto l = layoutpresets::load(id);
    REQUIRE(l.has_value());
    return *l;
}

int indexOf(const Layout& l, int id) {
    for (std::size_t i = 0; i < l.speakers.size(); ++i)
        if (l.speakers[i].id == id) return static_cast<int>(i);
    return -1;
}

}  // namespace

TEST_CASE("Direct: unit gain on each selected speaker, zero elsewhere") {
    const Layout l = preset("quad");
    const int a = l.speakers[0].id, b = l.speakers[2].id;
    Direct d({a, b});
    d.prepare(l, 48000.0, 64);
    std::vector<float> g(l.speakers.size(), -1.0f);
    d.gainsFor({45.0f, 10.0f, 3.0f}, 0.7f, g);
    for (std::size_t i = 0; i < g.size(); ++i) {
        const bool selected = static_cast<int>(i) == indexOf(l, a) || static_cast<int>(i) == indexOf(l, b);
        CHECK(g[i] == (selected ? 1.0f : 0.0f));
    }
}

TEST_CASE("Direct: position and spread do not change the gains") {
    const Layout l = preset("quad");
    Direct d({l.speakers[1].id});
    d.prepare(l, 48000.0, 64);
    std::vector<float> g1(l.speakers.size()), g2(l.speakers.size());
    d.gainsFor({0.0f, 0.0f, 1.0f}, 0.0f, g1);
    d.gainsFor({-170.0f, 60.0f, 9.0f}, 1.0f, g2);
    CHECK(g1 == g2);
}

TEST_CASE("Direct: routes to LFE speakers") {
    const Layout l = preset("5.1");
    int lfeId = -1;
    for (const auto& s : l.speakers)
        if (s.lfe) lfeId = s.id;
    REQUIRE(lfeId >= 0);
    Direct d({lfeId});
    d.prepare(l, 48000.0, 64);
    std::vector<float> g(l.speakers.size());
    d.gainsFor({}, 0.0f, g);
    CHECK(g[static_cast<std::size_t>(indexOf(l, lfeId))] == 1.0f);
}

TEST_CASE("Direct: unknown and duplicate speaker ids are ignored") {
    const Layout l = preset("stereo");
    Direct d({999, l.speakers[0].id, l.speakers[0].id});
    d.prepare(l, 48000.0, 64);
    CHECK(d.targets().size() == 1);
}

TEST_CASE("Direct: render adds the signal unchanged to the targets") {
    const Layout l = preset("quad");
    Direct d({l.speakers[3].id});
    d.prepare(l, 48000.0, 8);
    std::vector<std::vector<float>> buf(l.speakers.size(), std::vector<float>(8, 0.25f));
    std::vector<float*> ptrs;
    for (auto& b : buf) ptrs.push_back(b.data());
    SpeakerBus bus{ptrs.data(), static_cast<int>(ptrs.size()), nullptr, 0, 8};
    const float in[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    d.render(in, 8, {}, {}, 0.0f, bus);
    for (std::size_t s = 0; s < buf.size(); ++s)
        for (int i = 0; i < 8; ++i)
            CHECK(buf[s][static_cast<std::size_t>(i)] == (s == 3 ? 0.25f + in[i] : 0.25f));
}
