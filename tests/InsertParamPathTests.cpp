#include <catch2/catch_test_macros.hpp>

#include "core/InsertParamPath.h"

using namespace spacer;

TEST_CASE("InsertParamPath: parses the SPEC §13 source form and the speaker form") {
    const auto p = InsertParamPath::parse("source.3.insert.1.param.12");
    REQUIRE(p);
    CHECK(p->owner == InsertOwner::Source);
    CHECK(p->ownerId == 3);
    CHECK(p->slot == 1);
    CHECK(p->index == 12);
    CHECK(p->toString() == "source.3.insert.1.param.12");

    const auto s = InsertParamPath::parse("speaker.20.insert.0.param.0");
    REQUIRE(s);
    CHECK(s->owner == InsertOwner::Speaker);
    CHECK(s->ownerId == 20);
    CHECK(s->toString() == "speaker.20.insert.0.param.0");
}

TEST_CASE("InsertParamPath: unscoped tail") {
    const auto p = InsertParamPath::parseUnscoped("insert.3.param.7");
    REQUIRE(p);
    CHECK(p->ownerId == 0);
    CHECK(p->slot == 3);
    CHECK(p->index == 7);
}

TEST_CASE("InsertParamPath: rejects malformed paths") {
    for (const char* bad :
         {"", "source", "source.3", "source.3.insert", "source.3.insert.0", "source.3.insert.0.param",
          "source.3.insert.0.param.", "source.3.insert.0.param.x", "source.0.insert.0.param.1",
          "source.-1.insert.0.param.1", "source.3.insert.4.param.1", "source.3.insert.0.param.1.2",
          "master.3.insert.0.param.1", "source.3.insert.0.parameter.1", "source.3.gain",
          "source.3.insert.0.param.99999999999"})
        CHECK_FALSE(InsertParamPath::parse(bad));
    CHECK_FALSE(InsertParamPath::parseUnscoped("insert.0.param.1."));
    CHECK_FALSE(InsertParamPath::parseUnscoped("source.1.insert.0.param.1"));
}
