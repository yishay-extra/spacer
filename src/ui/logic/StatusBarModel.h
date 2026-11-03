#pragma once

// Status bar content (SPEC §16.6): CPU %, xruns, latency, clock (bpm, bar.beat)
// and clock-source lock, driver status. The app fills StatusInputs from the
// engine, FeedbackState and AppContext; this turns it into text and severity.
// Plain C++, unit-tested on Linux.

#include <cstdint>
#include <string>
#include <vector>

#include "core/Clock.h"
#include "core/Project.h"

namespace spacer::statusbar {

enum class Severity { Normal, Warning, Error };

enum class DriverState { Absent, Incompatible, NotResponding, Connected };  // as VirtualDeviceReceiver::State

struct Inputs {
    // Device (AudioEngine::Status).
    std::string deviceError;  // non-empty: the device could not be opened
    std::string deviceName;
    double sampleRate{0.0};
    int bufferSize{0};
    double cpuLoad{0.0};  // 0..1
    int xruns{0};
    int deviceLatencySamples{0};         // device input + output latency as reported
    int graphLatencySamples{0};          // limiter look-ahead (+ plugins later)
    int virtualDeviceLatencySamples{0};  // Spacer.driver hop when connected

    // Clock.
    bool hasClock{false};
    ClockState clock;
    ClockKind configuredClock{ClockKind::Internal};
    ClockKind activeClock{ClockKind::Internal};
    bool clockLocked{false};

    // Spacer.driver (SPEC §14).
    DriverState driverState{DriverState::Absent};
    std::string driverVersion;
    std::string driverWarning;  // AppContext::virtualDeviceWarning(); empty when connected
    std::uint64_t driverUnderrunFrames{0};

    // Cues.
    std::string armedCueList;
};

struct Item {
    std::string text;
    std::string tooltip;
    Severity severity{Severity::Normal};
};

/// The status bar, left to right.
struct Content {
    Item device;   // "Name  48 kHz / 128" or the error
    Item cpu;      // "CPU 12%"
    Item xruns;    // "xruns 0"
    Item latency;  // "Latency 5.3 ms"
    Item clock;    // "120.0 bpm  3.2" + source and lock
    Item cues;     // armed list, empty text if none
    Item driver;   // "Spacer.driver 0.1.0" / warning
    /// Offer "Install virtual device..." (driver absent or incompatible).
    bool offerDriverInstall{false};
};

Content compose(const Inputs& in);

// Building blocks (also used by the settings dialog).
std::string formatCpu(double load);
/// Milliseconds at `sampleRate`, one decimal ("5.3 ms"); "-" when the rate is unknown.
std::string formatMs(double samples, double sampleRate);
/// Latency added by Spacer: device buffer + limiter look-ahead (+ virtual
/// device hop when connected); the device-reported I/O latency is in the tooltip.
Item formatLatency(const Inputs& in);
/// 1-based "bar.beat" for a beat position (beats since the transport origin).
std::string formatBarBeat(double beat, int beatsPerBar);
Item formatClock(const Inputs& in);
Item formatDriver(const Inputs& in);

}  // namespace spacer::statusbar
