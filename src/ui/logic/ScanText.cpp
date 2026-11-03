#include "ui/logic/ScanText.h"

#include <algorithm>

#include "ui/logic/InsertEditing.h"

namespace spacer::scantext {

namespace {
std::string plural(int n, const char* one, const char* many) {
    return std::to_string(n) + " " + (n == 1 ? one : many);
}

std::string summary(const PluginScanProgress& p) {
    std::string out = std::to_string(p.added) + " added, " + std::to_string(p.skipped) + " up to date";
    if (!p.blacklisted.empty()) out += ", " + std::to_string(p.blacklisted.size()) + " blacklisted";
    return out;
}
}  // namespace

std::string status(const PluginScanProgress& p) {
    using S = PluginScanProgress::State;
    switch (p.state) {
        case S::Idle:
            return {};
        case S::Running: {
            std::string out = p.total > 0 ? "Scanning " + std::to_string(std::min(p.index + 1, p.total)) +
                                                " of " + std::to_string(p.total)
                                          : "Scanning";
            if (!p.current.empty()) out += ": " + insertedit::fallbackName(p.current);
            return out;
        }
        case S::Cancelling:
            return "Cancelling after the current plug-in...";
        case S::Finished:
            return "Scan finished: " + summary(p);
        case S::Cancelled:
            return "Scan cancelled: " + summary(p);
    }
    return {};
}

Buttons buttons(const PluginScanProgress& p) {
    Buttons b;
    b.scan = !p.active();
    b.rescan = !p.active();
    b.cancel = p.state == PluginScanProgress::State::Running;
    b.retry = !p.active();
    return b;
}

std::vector<BlacklistRow> blacklistRows(const std::vector<std::pair<std::string, std::string>>& entries) {
    std::vector<BlacklistRow> out;
    out.reserve(entries.size());
    for (const auto& [id, reason] : entries) out.push_back({id, insertedit::fallbackName(id), reason});
    return out;
}

std::string countsLine(int plugins, int blacklisted) {
    return plural(plugins, "plug-in", "plug-ins") + ", " + std::to_string(blacklisted) + " blacklisted";
}

}  // namespace spacer::scantext
