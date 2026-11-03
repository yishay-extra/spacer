#pragma once

// Progress of a background plugin scan (SPEC §13, WP11e). Plain data without
// JUCE, so the UI logic can turn it into text and tests can build it directly.

#include <string>
#include <vector>

namespace spacer {

struct PluginScanProgress {
    enum class State {
        Idle,  // nothing started yet
        Running,
        Cancelling,  // cancel requested; the plugin being scanned finishes first
        Finished,    // ran to the end
        Cancelled,   // stopped early; what was scanned is kept
    };
    State state{State::Idle};
    int index{0};         // identifiers handled so far (scanned or skipped), of `total`
    int total{0};         // candidates of all formats
    std::string current;  // identifier being scanned, "" between plugins
    int scanned{0};
    int skipped{0};  // cached and up to date, or blacklisted
    int added{0};
    std::vector<std::string> blacklisted;  // newly blacklisted by this scan

    bool active() const noexcept { return state == State::Running || state == State::Cancelling; }
    bool done() const noexcept { return state == State::Finished || state == State::Cancelled; }
    /// 0..1 for a progress bar (0 while the total is not known).
    double fraction() const noexcept {
        if (state == State::Finished) return 1.0;
        if (total <= 0) return 0.0;
        const double f = static_cast<double>(index) / static_cast<double>(total);
        return f < 0.0 ? 0.0 : (f > 1.0 ? 1.0 : f);
    }
};

}  // namespace spacer
