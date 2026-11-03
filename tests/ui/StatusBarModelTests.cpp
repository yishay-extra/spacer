#include <catch2/catch_test_macros.hpp>

#include "ui/logic/StatusBarModel.h"

using namespace spacer;
using namespace spacer::statusbar;

namespace {
Inputs running() {
    Inputs in;
    in.deviceName = "Interface";
    in.sampleRate = 48000.0;
    in.bufferSize = 128;
    in.graphLatencySamples = 96;
    in.hasClock = true;
    in.clock.bpm = 120.0;
    in.clock.beat = 5.5;
    in.clock.beatsPerBar = 4;
    in.driverState = DriverState::Connected;
    in.driverVersion = "0.1.0";
    return in;
}
}  // namespace

TEST_CASE("StatusBar: CPU and xruns", "[ui][status]") {
    auto in = running();
    in.cpuLoad = 0.123;
    auto c = compose(in);
    CHECK(c.cpu.text == "CPU 12%");
    CHECK(c.cpu.severity == Severity::Normal);
    CHECK(c.xruns.text == "xruns 0");
    CHECK(c.xruns.severity == Severity::Normal);
    in.cpuLoad = 0.85;
    in.xruns = 3;
    c = compose(in);
    CHECK(c.cpu.severity == Severity::Warning);
    CHECK(compose(running()).cpu.severity == Severity::Normal);
    CHECK(c.xruns.text == "xruns 3");
    CHECK(c.xruns.severity == Severity::Warning);
    in.cpuLoad = 0.97;
    CHECK(compose(in).cpu.severity == Severity::Error);
    CHECK(formatCpu(-1.0) == "CPU 0%");
}

TEST_CASE("StatusBar: latency adds buffer, look-ahead and the virtual device hop", "[ui][status]") {
    auto in = running();
    auto l = formatLatency(in);
    CHECK(l.text == "Latency 4.7 ms");  // (128 + 96) / 48 kHz
    in.virtualDeviceLatencySamples = 512;
    in.deviceLatencySamples = 240;
    l = formatLatency(in);
    CHECK(l.text == "Latency 15.3 ms");
    CHECK(l.tooltip.find("virtual device 10.7 ms") != std::string::npos);
    CHECK(l.tooltip.find("5.0 ms") != std::string::npos);  // device I/O
    CHECK(formatMs(100.0, 0.0) == "-");
}

TEST_CASE("StatusBar: device line and device error", "[ui][status]") {
    auto in = running();
    CHECK(compose(in).device.text == "Interface  48 kHz / 128");
    in.deviceError = "No such device";
    const auto c = compose(in);
    CHECK(c.device.severity == Severity::Error);
    CHECK(c.device.text.find("No such device") != std::string::npos);
}

TEST_CASE("StatusBar: clock shows bpm, bar.beat and the external source lock", "[ui][status]") {
    auto in = running();
    CHECK(formatBarBeat(0.0, 4) == "1.1");
    CHECK(formatBarBeat(5.5, 4) == "2.2");
    CHECK(formatBarBeat(8.0, 4) == "3.1");
    CHECK(formatBarBeat(-1.0, 4) == "1.1");
    CHECK(formatBarBeat(3.0, 0) == "4.1");  // beatsPerBar guarded
    CHECK(formatClock(in).text == "120.0 bpm  2.2");
    CHECK(formatClock(in).severity == Severity::Normal);

    in.configuredClock = ClockKind::Midi;
    in.activeClock = ClockKind::Midi;
    in.clockLocked = false;
    auto c = formatClock(in);
    CHECK(c.text == "120.0 bpm  2.2  MIDI clock (Searching...)");
    CHECK(c.severity == Severity::Warning);
    in.clockLocked = true;
    c = formatClock(in);
    CHECK(c.text == "120.0 bpm  2.2  MIDI clock (Locked)");
    CHECK(c.severity == Severity::Normal);

    in.configuredClock = ClockKind::Link;
    in.activeClock = ClockKind::Internal;  // Link not built
    c = formatClock(in);
    CHECK(c.text.find("Unavailable") != std::string::npos);
    CHECK(c.severity == Severity::Warning);

    in.hasClock = false;
    in.configuredClock = ClockKind::Osc;
    in.activeClock = ClockKind::Osc;
    CHECK(formatClock(in).text == "OSC (Locked)");
}

TEST_CASE("StatusBar: driver status and the install offer", "[ui][status]") {
    auto in = running();
    auto c = compose(in);
    CHECK(c.driver.text == "Spacer.driver 0.1.0");
    CHECK_FALSE(c.offerDriverInstall);
    in.driverUnderrunFrames = 12;
    CHECK(compose(in).driver.text == "Spacer.driver 0.1.0 (underruns 12)");
    CHECK(compose(in).driver.severity == Severity::Warning);

    in.driverState = DriverState::Absent;
    in.driverWarning = "Spacer.driver is not installed - 2 virtual-input source(s) silent";
    c = compose(in);
    CHECK(c.driver.text == in.driverWarning);
    CHECK(c.driver.severity == Severity::Warning);
    CHECK(c.offerDriverInstall);
    in.driverState = DriverState::Incompatible;
    CHECK(compose(in).offerDriverInstall);
    in.driverState = DriverState::NotResponding;
    CHECK_FALSE(compose(in).offerDriverInstall);
}

TEST_CASE("StatusBar: armed cue list", "[ui][status]") {
    auto in = running();
    CHECK(compose(in).cues.text.empty());
    in.armedCueList = "Show";
    CHECK(compose(in).cues.text == "Show");
}
