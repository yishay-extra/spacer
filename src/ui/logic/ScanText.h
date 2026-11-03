#pragma once

// Text for the plug-in scan window (SPEC §13, WP11e): status line, button
// states and the blacklist rows, derived from PluginScanProgress. Plain C++,
// unit-tested on Linux.

#include <string>
#include <utility>
#include <vector>

#include "engine/PluginScanProgress.h"

namespace spacer::scantext {

/// "Scanning 12 of 87: Effects/aufx,dely,appl", "Cancelling...", "Scan finished: 3 added,
/// 80 up to date, 2 blacklisted", "Scan cancelled: ...". "" when idle.
std::string status(const PluginScanProgress& progress);

struct Buttons {
    bool scan{false};    // start a scan of new / changed plug-ins
    bool rescan{false};  // rescan everything, blacklisted ones too
    bool cancel{false};
    bool retry{false};  // blacklist entries can be removed (no scan is running)
};
Buttons buttons(const PluginScanProgress& progress);

/// One blacklist row: the identifier's readable name and why it is blocked.
struct BlacklistRow {
    std::string identifier;
    std::string name;
    std::string reason;
};
std::vector<BlacklistRow> blacklistRows(const std::vector<std::pair<std::string, std::string>>& entries);

/// "3 plug-ins, 1 blacklisted" for the window title / summary line.
std::string countsLine(int plugins, int blacklisted);

}  // namespace spacer::scantext
