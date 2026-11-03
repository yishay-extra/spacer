#include <catch2/catch_test_macros.hpp>

#include "ui/logic/ScanText.h"

using namespace spacer;
using S = PluginScanProgress::State;

TEST_CASE("Scan progress: fraction, active and done", "[ui][plugins]") {
    PluginScanProgress p;
    CHECK_FALSE(p.active());
    CHECK_FALSE(p.done());
    CHECK(p.fraction() == 0.0);  // total unknown
    p.state = S::Running;
    p.total = 8;
    p.index = 2;
    CHECK(p.active());
    CHECK(p.fraction() == 0.25);
    p.index = 99;  // never beyond the bar
    CHECK(p.fraction() == 1.0);
    p.state = S::Cancelling;
    CHECK(p.active());
    p.state = S::Finished;
    CHECK(p.done());
    p.index = 0;
    CHECK(p.fraction() == 1.0);
    p.state = S::Cancelled;
    CHECK(p.done());
}

TEST_CASE("Scan text: status line for every state", "[ui][plugins]") {
    PluginScanProgress p;
    CHECK(scantext::status(p).empty());

    p.state = S::Running;
    CHECK(scantext::status(p) == "Scanning");
    p.total = 87;
    p.index = 11;
    p.current = "AudioUnit:Effects/aufx,dely,appl";
    CHECK(scantext::status(p) == "Scanning 12 of 87: aufx,dely,appl");

    p.state = S::Cancelling;
    CHECK(scantext::status(p) == "Cancelling after the current plug-in...");

    p.state = S::Finished;
    p.added = 3;
    p.skipped = 80;
    p.blacklisted = {"AudioUnit:Bad/x"};
    CHECK(scantext::status(p) == "Scan finished: 3 added, 80 up to date, 1 blacklisted");
    p.blacklisted.clear();
    CHECK(scantext::status(p) == "Scan finished: 3 added, 80 up to date");
    p.state = S::Cancelled;
    CHECK(scantext::status(p) == "Scan cancelled: 3 added, 80 up to date");
}

TEST_CASE("Scan text: buttons follow the scan state", "[ui][plugins]") {
    PluginScanProgress p;
    auto b = scantext::buttons(p);
    CHECK(b.scan);
    CHECK(b.rescan);
    CHECK(b.retry);
    CHECK_FALSE(b.cancel);

    p.state = S::Running;
    b = scantext::buttons(p);
    CHECK_FALSE(b.scan);
    CHECK_FALSE(b.rescan);
    CHECK_FALSE(b.retry);  // the cache is the scan's while it runs
    CHECK(b.cancel);

    p.state = S::Cancelling;
    b = scantext::buttons(p);
    CHECK_FALSE(b.cancel);  // already asked
    CHECK_FALSE(b.scan);

    p.state = S::Cancelled;
    CHECK(scantext::buttons(p).scan);
}

TEST_CASE("Scan text: blacklist rows and counts", "[ui][plugins]") {
    const auto rows = scantext::blacklistRows(
        {{"AudioUnit:Effects/aufx,bad!,appl", "crashed: scanner exited without a result (crashed)"}});
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].name == "aufx,bad!,appl");
    CHECK(rows[0].identifier == "AudioUnit:Effects/aufx,bad!,appl");
    CHECK(rows[0].reason.starts_with("crashed"));
    CHECK(scantext::countsLine(1, 0) == "1 plug-in, 0 blacklisted");
    CHECK(scantext::countsLine(12, 2) == "12 plug-ins, 2 blacklisted");
}
