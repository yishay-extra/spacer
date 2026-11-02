#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/Expected.h"

namespace spacer {

/// CoreAudio aggregate-device helper (macOS only): combine several devices
/// (e.g. the Spacer virtual device and an output interface) into one, so the
/// engine runs a single callback (SPEC §5.1, §16.5 "create aggregate").
/// Message thread only.
class AggregateDevice {
public:
    struct DeviceInfo {
        std::uint32_t id{0};  // AudioObjectID
        std::string name;
        std::string uid;
        int numInputs{0};
        int numOutputs{0};
    };

    /// All CoreAudio devices currently present.
    static std::vector<DeviceInfo> listDevices();

    /// Creates a public aggregate device. `subDeviceUids` in channel order;
    /// `clockUid` (one of them, empty = the first) is the clock source; the
    /// others get drift compensation when `driftCompensation` is set.
    /// Returns the new device's AudioObjectID.
    static Expected<std::uint32_t, std::string> create(const std::string& name, const std::string& uid,
                                                       const std::vector<std::string>& subDeviceUids,
                                                       const std::string& clockUid = {},
                                                       bool driftCompensation = true);

    /// Destroys an aggregate device created by create().
    static bool destroy(std::uint32_t deviceId);
};

}  // namespace spacer
