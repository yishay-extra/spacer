#include "ui/logic/StatusBarModel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "ui/logic/SettingsModel.h"

namespace spacer::statusbar {

namespace {

std::string fixed(double v, int decimals) {
    char buf[48];
    std::snprintf(buf, sizeof buf, "%.*f", decimals, v);
    return buf;
}

}  // namespace

std::string formatCpu(double load) {
    return "CPU " + fixed(std::clamp(load, 0.0, 9.99) * 100.0, 0) + "%";
}

std::string formatMs(double samples, double sampleRate) {
    if (sampleRate <= 0.0) return "-";
    return fixed(1000.0 * samples / sampleRate, 1) + " ms";
}

Item formatLatency(const Inputs& in) {
    Item item;
    const double added = in.bufferSize + in.graphLatencySamples + in.virtualDeviceLatencySamples;
    item.text = "Latency " + formatMs(added, in.sampleRate);
    item.tooltip = "Added by Spacer: device buffer " + formatMs(in.bufferSize, in.sampleRate) +
                   " + limiter look-ahead " + formatMs(in.graphLatencySamples, in.sampleRate);
    if (in.virtualDeviceLatencySamples > 0)
        item.tooltip += " + virtual device " + formatMs(in.virtualDeviceLatencySamples, in.sampleRate);
    item.tooltip +=
        ". Device I/O latency reported by the driver: " + formatMs(in.deviceLatencySamples, in.sampleRate);
    return item;
}

std::string formatBarBeat(double beat, int beatsPerBar) {
    const int perBar = std::max(1, beatsPerBar);
    const double b = std::max(0.0, beat);
    const int bar = static_cast<int>(std::floor(b / perBar)) + 1;
    const int inBar = static_cast<int>(std::floor(std::fmod(b, perBar))) + 1;
    return std::to_string(bar) + "." + std::to_string(inBar);
}

Item formatClock(const Inputs& in) {
    Item item;
    if (in.hasClock)
        item.text = fixed(in.clock.bpm, 1) + " bpm  " + formatBarBeat(in.clock.beat, in.clock.beatsPerBar);
    if (in.configuredClock != ClockKind::Internal) {
        if (!item.text.empty()) item.text += "  ";
        item.text += settings::clockKindName(in.configuredClock) + " (" +
                     settings::clockLockText(in.configuredClock, in.activeClock, in.clockLocked) + ")";
        if (in.activeClock == ClockKind::Internal)
            item.severity = Severity::Warning;
        else if (!in.clockLocked)
            item.severity = Severity::Warning;
    }
    item.tooltip = "Clock source: " + settings::clockKindName(in.configuredClock);
    return item;
}

Item formatDriver(const Inputs& in) {
    Item item;
    if (in.driverWarning.empty() && in.driverState == DriverState::Connected) {
        item.text = "Spacer.driver " + in.driverVersion;
        if (in.driverUnderrunFrames > 0) {
            item.text += " (underruns " + std::to_string(in.driverUnderrunFrames) + ")";
            item.severity = Severity::Warning;
        }
    } else {
        item.text = in.driverWarning.empty() ? "Spacer.driver not connected" : in.driverWarning;
        item.severity = Severity::Warning;
    }
    item.tooltip = "Virtual device (SPEC §14)";
    return item;
}

Content compose(const Inputs& in) {
    Content c;
    if (!in.deviceError.empty()) {
        c.device = {"Audio device error: " + in.deviceError, "", Severity::Error};
    } else {
        c.device.text = in.deviceName;
        if (in.sampleRate > 0.0)
            c.device.text +=
                "  " + settings::formatSampleRate(in.sampleRate) + " / " + std::to_string(in.bufferSize);
    }
    c.cpu.text = formatCpu(in.cpuLoad);
    c.cpu.severity = in.cpuLoad >= 0.95  ? Severity::Error
                     : in.cpuLoad >= 0.8 ? Severity::Warning
                                         : Severity::Normal;
    c.xruns.text = "xruns " + std::to_string(in.xruns);
    c.xruns.severity = in.xruns > 0 ? Severity::Warning : Severity::Normal;
    c.latency = formatLatency(in);
    c.clock = formatClock(in);
    c.cues.text = in.armedCueList;
    c.driver = formatDriver(in);
    c.offerDriverInstall =
        in.driverState == DriverState::Absent || in.driverState == DriverState::Incompatible;
    return c;
}

}  // namespace spacer::statusbar
