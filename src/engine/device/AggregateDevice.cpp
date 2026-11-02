#include "engine/device/AggregateDevice.h"

#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>

#include <cstring>

namespace spacer {

namespace {

/// Releases a Core Foundation object at scope exit.
template <typename T>
class CfRef {
public:
    explicit CfRef(T ref = nullptr) noexcept : ref_(ref) {}
    ~CfRef() {
        if (ref_ != nullptr) CFRelease(ref_);
    }
    CfRef(const CfRef&) = delete;
    CfRef& operator=(const CfRef&) = delete;
    T get() const noexcept { return ref_; }
    explicit operator bool() const noexcept { return ref_ != nullptr; }

private:
    T ref_;
};

std::string toStdString(CFStringRef s) {
    if (s == nullptr) return {};
    const CFIndex length = CFStringGetLength(s);
    const CFIndex capacity = CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
    std::string out(static_cast<std::size_t>(capacity), '\0');
    if (!CFStringGetCString(s, out.data(), capacity, kCFStringEncodingUTF8)) return {};
    out.resize(std::strlen(out.c_str()));
    return out;
}

CFStringRef makeCfString(const std::string& s) {
    return CFStringCreateWithCString(kCFAllocatorDefault, s.c_str(), kCFStringEncodingUTF8);
}

std::string stringProperty(AudioObjectID id, AudioObjectPropertySelector selector) {
    const AudioObjectPropertyAddress address{selector, kAudioObjectPropertyScopeGlobal,
                                             kAudioObjectPropertyElementMain};
    CFStringRef value = nullptr;
    UInt32 size = sizeof(value);
    if (AudioObjectGetPropertyData(id, &address, 0, nullptr, &size, &value) != noErr) return {};
    const CfRef<CFStringRef> owned(value);
    return toStdString(value);
}

int channelCount(AudioObjectID id, AudioObjectPropertyScope scope) {
    const AudioObjectPropertyAddress address{kAudioDevicePropertyStreamConfiguration, scope,
                                             kAudioObjectPropertyElementMain};
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(id, &address, 0, nullptr, &size) != noErr || size == 0) return 0;
    // AudioBufferList is variable-length; 8-byte storage keeps its alignment.
    std::vector<std::uint64_t> storage((size + sizeof(std::uint64_t) - 1) / sizeof(std::uint64_t));
    auto* list = reinterpret_cast<AudioBufferList*>(storage.data());
    if (AudioObjectGetPropertyData(id, &address, 0, nullptr, &size, list) != noErr) return 0;
    int channels = 0;
    const AudioBuffer* buffers = list->mBuffers;
    for (UInt32 i = 0; i < list->mNumberBuffers; ++i)
        channels += static_cast<int>(buffers[i].mNumberChannels);
    return channels;
}

void setInt(CFMutableDictionaryRef dict, CFStringRef key, int value) {
    const CfRef<CFNumberRef> number(CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &value));
    CFDictionarySetValue(dict, key, number.get());
}

}  // namespace

std::vector<AggregateDevice::DeviceInfo> AggregateDevice::listDevices() {
    const AudioObjectPropertyAddress address{kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal,
                                             kAudioObjectPropertyElementMain};
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &address, 0, nullptr, &size) != noErr)
        return {};
    std::vector<AudioObjectID> ids(size / sizeof(AudioObjectID));
    if (ids.empty() || AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0, nullptr, &size,
                                                  ids.data()) != noErr)
        return {};
    ids.resize(size / sizeof(AudioObjectID));

    std::vector<DeviceInfo> out;
    out.reserve(ids.size());
    for (const AudioObjectID id : ids) {
        DeviceInfo info;
        info.id = id;
        info.name = stringProperty(id, kAudioObjectPropertyName);
        info.uid = stringProperty(id, kAudioDevicePropertyDeviceUID);
        info.numInputs = channelCount(id, kAudioObjectPropertyScopeInput);
        info.numOutputs = channelCount(id, kAudioObjectPropertyScopeOutput);
        out.push_back(std::move(info));
    }
    return out;
}

Expected<std::uint32_t, std::string> AggregateDevice::create(const std::string& name, const std::string& uid,
                                                             const std::vector<std::string>& subDeviceUids,
                                                             const std::string& clockUid,
                                                             bool driftCompensation) {
    if (name.empty() || uid.empty())
        return Unexpected(std::string("aggregate device needs a name and a UID"));
    if (subDeviceUids.empty())
        return Unexpected(std::string("aggregate device needs at least one sub-device"));
    const std::string clock = clockUid.empty() ? subDeviceUids.front() : clockUid;

    const CfRef<CFMutableDictionaryRef> description(CFDictionaryCreateMutable(
        kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks));
    const CfRef<CFStringRef> cfName(makeCfString(name));
    const CfRef<CFStringRef> cfUid(makeCfString(uid));
    const CfRef<CFStringRef> cfClock(makeCfString(clock));
    if (!description || !cfName || !cfUid || !cfClock) return Unexpected(std::string("out of memory"));

    CFDictionarySetValue(description.get(), CFSTR(kAudioAggregateDeviceNameKey), cfName.get());
    CFDictionarySetValue(description.get(), CFSTR(kAudioAggregateDeviceUIDKey), cfUid.get());
    setInt(description.get(), CFSTR(kAudioAggregateDeviceIsPrivateKey), 0);
    setInt(description.get(), CFSTR(kAudioAggregateDeviceIsStackedKey), 0);

    const CfRef<CFMutableArrayRef> subDevices(
        CFArrayCreateMutable(kCFAllocatorDefault, 0, &kCFTypeArrayCallBacks));
    for (const auto& subUid : subDeviceUids) {
        const CfRef<CFMutableDictionaryRef> sub(CFDictionaryCreateMutable(
            kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks));
        const CfRef<CFStringRef> cfSub(makeCfString(subUid));
        CFDictionarySetValue(sub.get(), CFSTR(kAudioSubDeviceUIDKey), cfSub.get());
        if (driftCompensation && subUid != clock)
            setInt(sub.get(), CFSTR(kAudioSubDeviceDriftCompensationKey), 1);
        CFArrayAppendValue(subDevices.get(), sub.get());
    }
    CFDictionarySetValue(description.get(), CFSTR(kAudioAggregateDeviceSubDeviceListKey), subDevices.get());
    CFDictionarySetValue(description.get(), CFSTR(kAudioAggregateDeviceMainSubDeviceKey), cfClock.get());

    AudioObjectID device = kAudioObjectUnknown;
    const OSStatus status = AudioHardwareCreateAggregateDevice(description.get(), &device);
    if (status != noErr || device == kAudioObjectUnknown)
        return Unexpected("AudioHardwareCreateAggregateDevice failed (OSStatus " + std::to_string(status) +
                          ")");
    return static_cast<std::uint32_t>(device);
}

bool AggregateDevice::destroy(std::uint32_t deviceId) {
    return AudioHardwareDestroyAggregateDevice(static_cast<AudioObjectID>(deviceId)) == noErr;
}

}  // namespace spacer
