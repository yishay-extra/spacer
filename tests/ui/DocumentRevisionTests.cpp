#include <catch2/catch_test_macros.hpp>

#include "ui/logic/DocumentRevision.h"

using namespace spacer;

TEST_CASE("a new document is clean and the first edit makes it dirty", "[ui][revision]") {
    DocumentRevision r;
    REQUIRE_FALSE(r.isDirty());
    r.bump();
    REQUIRE(r.isDirty());
}

TEST_CASE("markSaved after the load notification leaves the document clean", "[ui][revision]") {
    DocumentRevision r;
    r.bump();  // the Loaded notification at launch
    r.markSaved();
    REQUIRE_FALSE(r.isDirty());
    r.bump();  // the first edit
    REQUIRE(r.isDirty());
    r.markSaved();
    REQUIRE_FALSE(r.isDirty());
}
