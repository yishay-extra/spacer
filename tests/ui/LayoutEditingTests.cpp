#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/LayoutPresets.h"
#include "core/Validate.h"
#include "ui/logic/LayoutEditing.h"

using namespace spacer;
using namespace spacer::layoutedit;
using Catch::Matchers::WithinAbs;

namespace {

Layout quad() {
    auto l = layoutpresets::load("quad");
    REQUIRE(l);
    return *l;
}

}  // namespace

TEST_CASE("Added speakers get a fresh id, name and the next free channel", "[ui][layout]") {
    Layout l = quad();
    l.speakers[1].outputChannel = 7;  // channels 0, 7, 2, 3 -> 1 is free
    const auto id = addSpeaker(l, {45, 10, 3});
    REQUIRE(id);
    REQUIRE(*id == nextSpeakerId(quad()));
    const Speaker& s = l.speakers.back();
    REQUIRE(s.id == *id);
    REQUIRE(s.name == std::to_string(*id));
    REQUIRE(s.outputChannel == 1);
    REQUIRE(s.position == Position{45, 10, 3});
    REQUIRE_FALSE(hasErrors(validate(l)));
}

TEST_CASE("A full layout refuses more speakers", "[ui][layout]") {
    Layout l;
    for (int i = 0; i < kMaxSpeakers; ++i) REQUIRE(addSpeaker(l, {static_cast<float>(i * 10), 0, 2}));
    const Layout before = l;
    REQUIRE_FALSE(addSpeaker(l, {0, 0, 2}));
    REQUIRE_FALSE(duplicateSpeaker(l, 1));
    REQUIRE(l == before);
}

TEST_CASE("Removing speakers keeps the SPEC minimum of two", "[ui][layout]") {
    Layout l = quad();
    const int first = l.speakers[0].id;
    REQUIRE(removeSpeaker(l, first));
    REQUIRE(l.findSpeaker(first) == nullptr);
    REQUIRE_FALSE(removeSpeaker(l, 999));
    REQUIRE(removeSpeaker(l, l.speakers[0].id));
    REQUIRE(l.speakers.size() == 2);
    REQUIRE_FALSE(removeSpeaker(l, l.speakers[0].id));
    REQUIRE(l.speakers.size() == 2);
}

TEST_CASE("Duplicating inserts a copy after the original", "[ui][layout]") {
    Layout l = quad();
    l.speakers[0].trimDb = -3.0f;
    const int orig = l.speakers[0].id;
    const auto copy = duplicateSpeaker(l, orig);
    REQUIRE(copy);
    REQUIRE(l.speakers[1].id == *copy);
    REQUIRE(l.speakers[1].trimDb == -3.0f);
    REQUIRE(l.speakers[1].name == l.speakers[0].name + " copy");
    REQUIRE(l.speakers[1].outputChannel == 4);
    REQUIRE_FALSE(hasErrors(validate(l)));
}

TEST_CASE("Moving a speaker normalises and keeps it off the origin", "[ui][layout]") {
    Layout l = quad();
    const int id = l.speakers[0].id;
    REQUIRE(moveSpeaker(l, id, {270, 100, 0}));
    const auto& p = l.findSpeaker(id)->position;
    REQUIRE_THAT(p.azimuth, WithinAbs(-90.0, 1e-4));
    REQUIRE(p.elevation == 90.0f);
    REQUIRE(p.distance == kMinSpeakerDistance);
    REQUIRE_FALSE(moveSpeaker(l, 999, {}));
    REQUIRE_FALSE(moveSpeaker(l, id, {std::numeric_limits<float>::quiet_NaN(), 0, 1}));
}

TEST_CASE("Sequential output assignment numbers speakers in order", "[ui][layout]") {
    Layout l = quad();
    for (auto& s : l.speakers) s.outputChannel = -1;
    assignOutputsSequentially(l);
    for (std::size_t i = 0; i < l.speakers.size(); ++i)
        REQUIRE(l.speakers[i].outputChannel == static_cast<int>(i));
}

TEST_CASE("Every preset can be applied and validates", "[ui][layout]") {
    for (const auto& id : layoutpresets::ids()) {
        const auto l = preset(id);
        REQUIRE(l);
        REQUIRE_FALSE(hasErrors(validate(*l)));
    }
    REQUIRE_FALSE(preset("no-such-layout"));
}

TEST_CASE("Table fields format and parse", "[ui][layout]") {
    Speaker s{3, "Ls", {-110, 0, 2}, -1.5f, 0.25f, false, 4, false};
    REQUIRE(formatField(s, SpeakerField::Name) == "Ls");
    REQUIRE(formatField(s, SpeakerField::Azimuth) == "-110.0");
    REQUIRE(formatField(s, SpeakerField::Distance) == "2.00");
    REQUIRE(formatField(s, SpeakerField::TrimDb) == "-1.5");
    REQUIRE(formatField(s, SpeakerField::OutputChannel) == "5");  // 1-based in the UI
    REQUIRE(formatField(s, SpeakerField::Polarity).empty());
    s.outputChannel = -1;
    REQUIRE(formatField(s, SpeakerField::OutputChannel).empty());
    REQUIRE(formatField(Speaker{1, "x", {-0.01f, 0, 1}}, SpeakerField::Azimuth) == "0.0");

    REQUIRE(setField(s, SpeakerField::Azimuth, " 200 "));
    REQUIRE_THAT(s.position.azimuth, WithinAbs(-160.0, 1e-4));
    REQUIRE(setField(s, SpeakerField::OutputChannel, "12"));
    REQUIRE(s.outputChannel == 11);
    REQUIRE(setField(s, SpeakerField::OutputChannel, "0"));
    REQUIRE(s.outputChannel == -1);
    REQUIRE(setField(s, SpeakerField::Polarity, "inv"));
    REQUIRE(s.polarityInverted);
    REQUIRE(setField(s, SpeakerField::Polarity, "no"));
    REQUIRE_FALSE(s.polarityInverted);
    REQUIRE(setField(s, SpeakerField::Lfe, "LFE"));
    REQUIRE(s.lfe);
    REQUIRE(setField(s, SpeakerField::DelayMs, "+1.5"));
    REQUIRE(s.userDelayMs == 1.5f);
    REQUIRE(setField(s, SpeakerField::Name, "  Left surround "));
    REQUIRE(s.name == "Left surround");
}

TEST_CASE("Invalid table input leaves the speaker unchanged", "[ui][layout]") {
    const Speaker orig{3, "Ls", {-110, 0, 2}, -1.5f, 0.25f, false, 4, false};
    Speaker s = orig;
    REQUIRE_FALSE(setField(s, SpeakerField::Azimuth, "abc"));
    REQUIRE_FALSE(setField(s, SpeakerField::Azimuth, "12x"));
    REQUIRE_FALSE(setField(s, SpeakerField::Azimuth, "inf"));
    REQUIRE_FALSE(setField(s, SpeakerField::Elevation, "91"));
    REQUIRE_FALSE(setField(s, SpeakerField::Distance, "0"));
    REQUIRE_FALSE(setField(s, SpeakerField::TrimDb, "20"));
    REQUIRE_FALSE(setField(s, SpeakerField::DelayMs, "-1"));
    REQUIRE_FALSE(setField(s, SpeakerField::DelayMs, "101"));
    REQUIRE_FALSE(setField(s, SpeakerField::OutputChannel, "-3"));
    REQUIRE_FALSE(setField(s, SpeakerField::OutputChannel, "two"));
    REQUIRE_FALSE(setField(s, SpeakerField::Name, "   "));
    REQUIRE_FALSE(setField(s, SpeakerField::Lfe, "maybe"));
    REQUIRE(s == orig);
}

TEST_CASE("Compensation rows match computeCompensation and chain overrides", "[ui][layout]") {
    Project p;
    p.layout = quad();
    p.layout.speakers[0].position.distance = 1.0f;  // nearer: delayed and attenuated
    p.layout.speakers[1].trimDb = -2.0f;
    p.devices.speedOfSound = 340.0f;
    SpeakerChainSpec chain;
    chain.speakerId = p.layout.speakers[2].id;
    chain.trimDb = -6.0f;
    chain.userDelayMs = 3.0f;
    p.speakerChains.push_back(chain);

    const auto rows = compensationRows(p);
    const auto comp = computeCompensation(p.layout, 340.0f);
    REQUIRE(rows.size() == 4);
    for (std::size_t i = 0; i < rows.size(); ++i) {
        REQUIRE(rows[i].speakerId == p.layout.speakers[i].id);
        REQUIRE(rows[i].compensationDelayMs == comp[i].delayMs);
        REQUIRE(rows[i].compensationGainDb == comp[i].gainDb);
    }
    REQUIRE_THAT(rows[0].compensationDelayMs, WithinAbs(1000.0 / 340.0, 1e-3));
    REQUIRE_THAT(rows[0].effectiveGainDb(), WithinAbs(20.0 * std::log10(0.5), 1e-3));
    REQUIRE(rows[1].effectiveGainDb() == rows[1].compensationGainDb - 2.0f);
    REQUIRE(rows[2].overriddenByChain);
    REQUIRE(rows[2].effectiveGainDb() == -6.0f);
    REQUIRE(rows[2].effectiveDelayMs() == 3.0f);
    REQUIRE_FALSE(rows[3].overriddenByChain);
}

TEST_CASE("Editing a field reports only real changes", "[ui][layout]") {
    Layout l = quad();
    const int id = l.speakers[0].id;
    const Layout before = l;
    REQUIRE_FALSE(editSpeakerField(l, id, SpeakerField::Azimuth, "nope"));
    REQUIRE_FALSE(editSpeakerField(l, id, SpeakerField::Name, l.speakers[0].name));  // unchanged
    REQUIRE_FALSE(editSpeakerField(l, 999, SpeakerField::Name, "x"));
    REQUIRE(l == before);
    REQUIRE(editSpeakerField(l, id, SpeakerField::TrimDb, "-4"));
    REQUIRE(l.speakers[0].trimDb == -4.0f);
}

TEST_CASE("Speaker table shows a chain's override of trim / delay / polarity (SPEC §7, WP14)",
          "[ui][layout]") {
    Project p;
    p.layout = quad();
    p.layout.speakers[2].trimDb = -1.0f;
    SpeakerChainSpec chain;
    chain.speakerId = p.layout.speakers[2].id;
    chain.trimDb = -6.0f;
    chain.userDelayMs = 3.0f;
    chain.polarityInverted = true;
    p.speakerChains.push_back(chain);

    const Speaker& plain = p.layout.speakers[0];
    const auto free = tableCell(p, plain, SpeakerField::TrimDb);
    CHECK(free.editable);
    CHECK_FALSE(free.fromChain);
    CHECK(free.text == formatField(plain, SpeakerField::TrimDb));

    const Speaker& overridden = p.layout.speakers[2];
    const auto trim = tableCell(p, overridden, SpeakerField::TrimDb);
    CHECK(trim.fromChain);
    CHECK_FALSE(trim.editable);
    CHECK(trim.text.find("-6") == 0);
    CHECK(trim.text.find("(chain)") != std::string::npos);
    CHECK(trim.tooltip.find(formatField(overridden, SpeakerField::TrimDb)) != std::string::npos);
    CHECK(tableCell(p, overridden, SpeakerField::DelayMs).text.find("3") == 0);
    CHECK(tableCell(p, overridden, SpeakerField::Polarity).text == "inv (chain)");
    // Fields a chain does not override stay editable layout values.
    const auto az = tableCell(p, overridden, SpeakerField::Azimuth);
    CHECK(az.editable);
    CHECK_FALSE(az.fromChain);
}
