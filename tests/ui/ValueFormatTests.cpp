#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "ui/logic/ValueFormat.h"

using namespace spacer;
using Catch::Matchers::WithinAbs;

TEST_CASE("Numbers are formatted without trailing zeros", "[ui][format]") {
    REQUIRE(fmt::number(2.0) == "2");
    REQUIRE(fmt::number(2.5) == "2.5");
    REQUIRE(fmt::number(-0.254, 2) == "-0.25");
    REQUIRE(fmt::number(-0.0001, 2) == "0");  // no "-0"
    REQUIRE(fmt::number(100.0, 0) == "100");  // integers keep their zeros
}

TEST_CASE("Values are formatted with their unit", "[ui][format]") {
    REQUIRE(fmt::format(-6.0f, fmt::Unit::Db) == "-6 dB");
    REQUIRE(fmt::format(fmt::kGainFloorDb, fmt::Unit::Db) == "-inf dB");
    REQUIRE(fmt::format(1.5f, fmt::Unit::Metres) == "1.5 m");
    REQUIRE(fmt::format(0.25f, fmt::Unit::Percent) == "25 %");
    REQUIRE(fmt::format(20.0f, fmt::Unit::Millis) == "20 ms");
    REQUIRE(fmt::format(440.0f, fmt::Unit::Hertz) == "440 Hz");
    REQUIRE(fmt::format(30.0f, fmt::Unit::Degrees) == "30\xC2\xB0");
    REQUIRE(fmt::format(3.0f, fmt::Unit::Count) == "3");
    REQUIRE(fmt::formatBare(0.5f, fmt::Unit::Percent) == "50");
}

TEST_CASE("Parsing accepts the unit suffix and rejects garbage", "[ui][format]") {
    REQUIRE_THAT(*fmt::parse("-6", fmt::Unit::Db), WithinAbs(-6.0, 1e-6));
    REQUIRE_THAT(*fmt::parse(" -6.5 dB ", fmt::Unit::Db), WithinAbs(-6.5, 1e-6));
    REQUIRE_THAT(*fmt::parse("-6DB", fmt::Unit::Db), WithinAbs(-6.0, 1e-6));
    REQUIRE(*fmt::parse("-inf", fmt::Unit::Db) == fmt::kGainFloorDb);
    REQUIRE(*fmt::parse("off", fmt::Unit::Db) == fmt::kGainFloorDb);
    REQUIRE_THAT(*fmt::parse("30\xC2\xB0", fmt::Unit::Degrees), WithinAbs(30.0, 1e-6));
    REQUIRE_THAT(*fmt::parse("30 deg", fmt::Unit::Degrees), WithinAbs(30.0, 1e-6));
    REQUIRE_THAT(*fmt::parse("1.5m", fmt::Unit::Metres), WithinAbs(1.5, 1e-6));
    REQUIRE_THAT(*fmt::parse("25%", fmt::Unit::Percent), WithinAbs(0.25, 1e-6));
    REQUIRE_THAT(*fmt::parse("25", fmt::Unit::Percent), WithinAbs(0.25, 1e-6));
    REQUIRE_THAT(*fmt::parse("2.5 s", fmt::Unit::Seconds), WithinAbs(2.5, 1e-6));
    REQUIRE_THAT(*fmt::parse("20ms", fmt::Unit::Millis), WithinAbs(20.0, 1e-6));
    REQUIRE_THAT(*fmt::parse("440 hz", fmt::Unit::Hertz), WithinAbs(440.0, 1e-6));

    for (const char* bad : {"", "abc", "1.2.3", "5 apples", "nan", "inf", "1e999", "--1"})
        REQUIRE_FALSE(fmt::parse(bad, fmt::Unit::None));
    REQUIRE_FALSE(fmt::parse("5 m", fmt::Unit::Db));  // wrong suffix
}

TEST_CASE("Clamped parsing keeps values in range", "[ui][format]") {
    REQUIRE(*fmt::parseClamped("99", fmt::Unit::Db, -100.0f, 12.0f) == 12.0f);
    REQUIRE(*fmt::parseClamped("-500", fmt::Unit::Db, -100.0f, 12.0f) == -100.0f);
    REQUIRE_FALSE(fmt::parseClamped("x", fmt::Unit::Db, -100.0f, 12.0f));
}

TEST_CASE("Format then parse round-trips", "[ui][format]") {
    for (auto unit : {fmt::Unit::Db, fmt::Unit::Degrees, fmt::Unit::Metres, fmt::Unit::Percent,
                      fmt::Unit::Seconds, fmt::Unit::Millis, fmt::Unit::Hertz}) {
        const float v = unit == fmt::Unit::Percent ? 0.375f : 12.5f;
        const auto back = fmt::parse(fmt::format(v, unit), unit);
        REQUIRE(back);
        REQUIRE_THAT(*back, WithinAbs(v, 1e-4));
    }
}

TEST_CASE("Periods carry beats or seconds", "[ui][format]") {
    REQUIRE(fmt::formatPeriod(Period::beats(8)) == "8 beats");
    REQUIRE(fmt::formatPeriod(Period::seconds(2.5)) == "2.5 s");
    const Period beats = Period::beats(4), secs = Period::seconds(4);
    REQUIRE(*fmt::parsePeriod("8", beats) == Period::beats(8));
    REQUIRE(*fmt::parsePeriod("8", secs) == Period::seconds(8));  // bare number keeps the unit
    REQUIRE(*fmt::parsePeriod("8b", secs) == Period::beats(8));
    REQUIRE(*fmt::parsePeriod("8 beats", secs) == Period::beats(8));
    REQUIRE(*fmt::parsePeriod("2.5s", beats) == Period::seconds(2.5));
    REQUIRE(*fmt::parsePeriod("2.5 sec", beats) == Period::seconds(2.5));
    REQUIRE_FALSE(fmt::parsePeriod("0", beats));
    REQUIRE_FALSE(fmt::parsePeriod("-2", beats));
    REQUIRE_FALSE(fmt::parsePeriod("fast", beats));
    REQUIRE_FALSE(fmt::parsePeriod("", beats));
}
