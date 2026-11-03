// Spacer.driver — CoreAudio AudioServerPlugIn (SPEC §14).
//
// One device, "Spacer", with one output stream of 64 float32 channels (as seen
// by sender apps). Whatever the clients mix into it (WriteMix) is appended to
// the shared-memory ring in driver/protocol, from which the Spacer app reads.
// The device runs on the host clock at its nominal rate; rate differences to
// the app's output device are absorbed app-side (VirtualDeviceReceiver).
//
// Objects: the plug-in (kAudioObjectPlugInObject), the device, its output
// stream. No box, no controls.
//
// Threads: property calls come from coreaudiod's (non-RT) threads and take
// gStateMutex. The IO calls (StartIO/StopIO excepted) are real-time: atomics
// only. A dispatch timer increments the shared heartbeat and forwards the
// app's requested sample rate as a device configuration change.
//
// No JUCE, no spacer_core (CLAUDE.md); only the protocol headers.

#include <CoreAudio/AudioServerPlugIn.h>
#include <CoreFoundation/CFPlugInCOM.h>
#include <dispatch/dispatch.h>
#include <mach/mach_time.h>
#include <os/log.h>
#include <pthread.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <string>

#include "vdev/Protocol.h"
#include "vdev/RingWriter.h"
#include "vdev/SharedMemory.h"

#ifndef SPACER_DRIVER_VERSION
#define SPACER_DRIVER_VERSION "0.0.0"
#endif

namespace {

using spacer::vdev::kNumChannels;

// ---- object model ----------------------------------------------------------
enum : AudioObjectID {
    kObjectID_PlugIn = kAudioObjectPlugInObject,
    kObjectID_Device = 2,
    kObjectID_Stream = 3,
};

template <typename T>
constexpr UInt32 sizeOf = static_cast<UInt32>(sizeof(T));

constexpr UInt32 kBytesPerFrame = kNumChannels * sizeOf<Float32>;
constexpr UInt32 kZeroTimeStampPeriod = spacer::vdev::kRingFrames;
constexpr UInt32 kNumSampleRates =
    static_cast<UInt32>(sizeof(spacer::vdev::kSupportedSampleRates) / sizeof(double));

#define kDeviceUID "org.spacer.driver.device"
#define kDeviceModelUID "org.spacer.driver.model"
#define kManufacturer "Spacer"

// ---- state -------------------------------------------------------------------
pthread_mutex_t gStateMutex = PTHREAD_MUTEX_INITIALIZER;
std::atomic<UInt32> gRefCount{0};
AudioServerPlugInHostRef gHost = nullptr;

// Guarded by gStateMutex (written), read lock-free where noted.
std::atomic<Float64> gSampleRate{spacer::vdev::kDefaultSampleRate};
bool gStreamActive = true;
UInt32 gIOCount = 0;  // clients doing IO
Float64 gLastRequestedRate = 0.0;

// IO timeline (written in StartIO / config change while IO is stopped, read
// by GetZeroTimeStamp on the IO thread).
std::atomic<Float64> gHostTicksPerFrame{0.0};
std::atomic<UInt64> gAnchorHostTime{0};
std::atomic<UInt64> gNumberTimeStamps{0};

// Shared memory (created once in Initialize, kept for the process lifetime).
spacer::vdev::SharedMemory gMemory;
spacer::vdev::RingWriter gWriter;
dispatch_source_t gTimer = nullptr;

os_log_t logger() {
    static os_log_t log = os_log_create("org.spacer.driver", "driver");
    return log;
}

const char* sharedMemoryName() {
    // Overridable for the in-process test harness; coreaudiod sets no such variable.
    const char* env = std::getenv("SPACER_DRIVER_SHM_NAME");
    return env != nullptr && env[0] == '/' ? env : spacer::vdev::kSharedMemoryName;
}

spacer::vdev::Header* header() {
    return gWriter.valid() ? gWriter.header() : nullptr;
}

Float64 hostTicksPerSecond() {
    mach_timebase_info_data_t tb{};
    mach_timebase_info(&tb);
    return 1e9 * static_cast<Float64>(tb.denom) / static_cast<Float64>(tb.numer);
}

void updateTimeline(Float64 sampleRate) {
    gHostTicksPerFrame.store(hostTicksPerSecond() / sampleRate);
}

void notify(AudioObjectID object, std::initializer_list<AudioObjectPropertySelector> selectors) {
    if (gHost == nullptr) return;
    AudioObjectPropertyAddress addresses[8];
    UInt32 n = 0;
    for (auto s : selectors)
        if (n < 8) addresses[n++] = {s, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    gHost->PropertiesChanged(gHost, object, n, addresses);
}

// Heartbeat + forwarding the app's sample-rate request (SPEC §14: the device
// follows the rate the Spacer app sets).
void timerTick() {
    auto* h = header();
    if (h == nullptr) return;
    h->heartbeat.fetch_add(1, std::memory_order_relaxed);
    const Float64 wanted = h->requestedSampleRate.load(std::memory_order_relaxed);
    if (!spacer::vdev::isSupportedSampleRate(wanted)) return;
    pthread_mutex_lock(&gStateMutex);
    const bool request = wanted != gSampleRate.load() && wanted != gLastRequestedRate;
    if (request) gLastRequestedRate = wanted;
    pthread_mutex_unlock(&gStateMutex);
    if (request && gHost != nullptr) {
        os_log(logger(), "app requests %.0f Hz", wanted);
        gHost->RequestDeviceConfigurationChange(gHost, kObjectID_Device, static_cast<UInt64>(wanted),
                                                nullptr);
    }
}

void startSharedMemory() {
    std::string error;
    const char* name = sharedMemoryName();
    gMemory = spacer::vdev::SharedMemory::create(name, spacer::vdev::kRegionBytes, &error);
    if (!gMemory.valid()) {
        os_log_error(logger(), "cannot create shared memory %{public}s: %{public}s", name, error.c_str());
        return;
    }
    const auto session = (static_cast<std::uint64_t>(getpid()) << 32) ^ mach_absolute_time();
    spacer::vdev::initialiseHeader(gMemory.data(), session, SPACER_DRIVER_VERSION, gSampleRate.load());
    gWriter = spacer::vdev::RingWriter(gMemory.data());

    gTimer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0,
                                    dispatch_get_global_queue(QOS_CLASS_UTILITY, 0));
    const auto interval = static_cast<uint64_t>(spacer::vdev::kHeartbeatIntervalMs) * NSEC_PER_MSEC;
    dispatch_source_set_timer(gTimer, dispatch_time(DISPATCH_TIME_NOW, 0), interval, interval / 10);
    dispatch_source_set_event_handler_f(gTimer, [](void*) { timerTick(); });
    dispatch_resume(gTimer);
    os_log(logger(), "Spacer.driver %{public}s ready, shared memory %{public}s", SPACER_DRIVER_VERSION, name);
}

AudioStreamBasicDescription streamFormat(Float64 sampleRate) {
    AudioStreamBasicDescription f{};
    f.mSampleRate = sampleRate;
    f.mFormatID = kAudioFormatLinearPCM;
    f.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagsNativeEndian | kAudioFormatFlagIsPacked;
    f.mBytesPerPacket = kBytesPerFrame;
    f.mFramesPerPacket = 1;
    f.mBytesPerFrame = kBytesPerFrame;
    f.mChannelsPerFrame = kNumChannels;
    f.mBitsPerChannel = 32;
    return f;
}

UInt32 channelLayoutSize() {
    return static_cast<UInt32>(offsetof(AudioChannelLayout, mChannelDescriptions) +
                               kNumChannels * sizeOf<AudioChannelDescription>);
}

// ---- COM plumbing ----------------------------------------------------------
HRESULT QueryInterface(void* driver, REFIID uuid, LPVOID* out);
ULONG AddRef(void* driver);
ULONG Release(void* driver);
OSStatus Initialize(AudioServerPlugInDriverRef driver, AudioServerPlugInHostRef host);
OSStatus CreateDevice(AudioServerPlugInDriverRef, CFDictionaryRef, const AudioServerPlugInClientInfo*,
                      AudioObjectID*);
OSStatus DestroyDevice(AudioServerPlugInDriverRef, AudioObjectID);
OSStatus AddDeviceClient(AudioServerPlugInDriverRef, AudioObjectID, const AudioServerPlugInClientInfo*);
OSStatus RemoveDeviceClient(AudioServerPlugInDriverRef, AudioObjectID, const AudioServerPlugInClientInfo*);
OSStatus PerformDeviceConfigurationChange(AudioServerPlugInDriverRef, AudioObjectID, UInt64, void*);
OSStatus AbortDeviceConfigurationChange(AudioServerPlugInDriverRef, AudioObjectID, UInt64, void*);
Boolean HasProperty(AudioServerPlugInDriverRef, AudioObjectID, pid_t, const AudioObjectPropertyAddress*);
OSStatus IsPropertySettable(AudioServerPlugInDriverRef, AudioObjectID, pid_t,
                            const AudioObjectPropertyAddress*, Boolean*);
OSStatus GetPropertyDataSize(AudioServerPlugInDriverRef, AudioObjectID, pid_t,
                             const AudioObjectPropertyAddress*, UInt32, const void*, UInt32*);
OSStatus GetPropertyData(AudioServerPlugInDriverRef, AudioObjectID, pid_t, const AudioObjectPropertyAddress*,
                         UInt32, const void*, UInt32, UInt32*, void*);
OSStatus SetPropertyData(AudioServerPlugInDriverRef, AudioObjectID, pid_t, const AudioObjectPropertyAddress*,
                         UInt32, const void*, UInt32, const void*);
OSStatus StartIO(AudioServerPlugInDriverRef, AudioObjectID, UInt32);
OSStatus StopIO(AudioServerPlugInDriverRef, AudioObjectID, UInt32);
OSStatus GetZeroTimeStamp(AudioServerPlugInDriverRef, AudioObjectID, UInt32, Float64*, UInt64*, UInt64*);
OSStatus WillDoIOOperation(AudioServerPlugInDriverRef, AudioObjectID, UInt32, UInt32, Boolean*, Boolean*);
OSStatus BeginIOOperation(AudioServerPlugInDriverRef, AudioObjectID, UInt32, UInt32, UInt32,
                          const AudioServerPlugInIOCycleInfo*);
OSStatus DoIOOperation(AudioServerPlugInDriverRef, AudioObjectID, AudioObjectID, UInt32, UInt32, UInt32,
                       const AudioServerPlugInIOCycleInfo*, void*, void*);
OSStatus EndIOOperation(AudioServerPlugInDriverRef, AudioObjectID, UInt32, UInt32, UInt32,
                        const AudioServerPlugInIOCycleInfo*);

AudioServerPlugInDriverInterface gInterface = {
    nullptr,
    QueryInterface,
    AddRef,
    Release,
    Initialize,
    CreateDevice,
    DestroyDevice,
    AddDeviceClient,
    RemoveDeviceClient,
    PerformDeviceConfigurationChange,
    AbortDeviceConfigurationChange,
    HasProperty,
    IsPropertySettable,
    GetPropertyDataSize,
    GetPropertyData,
    SetPropertyData,
    StartIO,
    StopIO,
    GetZeroTimeStamp,
    WillDoIOOperation,
    BeginIOOperation,
    DoIOOperation,
    EndIOOperation,
};
AudioServerPlugInDriverInterface* gInterfacePtr = &gInterface;
AudioServerPlugInDriverRef gDriverRef = &gInterfacePtr;

bool isDriver(AudioServerPlugInDriverRef driver) {
    return driver == gDriverRef;
}

HRESULT QueryInterface(void* driver, REFIID uuid, LPVOID* out) {
    if (driver != gDriverRef || out == nullptr) return kAudioHardwareBadObjectError;
    CFUUIDRef requested = CFUUIDCreateFromUUIDBytes(nullptr, uuid);
    if (requested == nullptr) return kAudioHardwareIllegalOperationError;
    const bool known =
        CFEqual(requested, IUnknownUUID) || CFEqual(requested, kAudioServerPlugInDriverInterfaceUUID);
    CFRelease(requested);
    if (!known) return E_NOINTERFACE;
    gRefCount.fetch_add(1);
    *out = gDriverRef;
    return S_OK;
}

ULONG AddRef(void* driver) {
    if (driver != gDriverRef) return 0;
    return gRefCount.fetch_add(1) + 1;
}

ULONG Release(void* driver) {
    if (driver != gDriverRef) return 0;
    UInt32 current = gRefCount.load();
    while (current > 0 && !gRefCount.compare_exchange_weak(current, current - 1)) {
    }
    return current > 0 ? current - 1 : 0;
}

OSStatus Initialize(AudioServerPlugInDriverRef driver, AudioServerPlugInHostRef host) {
    if (!isDriver(driver)) return kAudioHardwareBadObjectError;
    gHost = host;
    updateTimeline(gSampleRate.load());
    if (!gMemory.valid()) startSharedMemory();
    return kAudioHardwareNoError;
}

OSStatus CreateDevice(AudioServerPlugInDriverRef, CFDictionaryRef, const AudioServerPlugInClientInfo*,
                      AudioObjectID*) {
    return kAudioHardwareUnsupportedOperationError;
}

OSStatus DestroyDevice(AudioServerPlugInDriverRef, AudioObjectID) {
    return kAudioHardwareUnsupportedOperationError;
}

OSStatus AddDeviceClient(AudioServerPlugInDriverRef driver, AudioObjectID device,
                         const AudioServerPlugInClientInfo*) {
    if (!isDriver(driver)) return kAudioHardwareBadObjectError;
    return device == kObjectID_Device ? kAudioHardwareNoError : kAudioHardwareBadObjectError;
}

OSStatus RemoveDeviceClient(AudioServerPlugInDriverRef driver, AudioObjectID device,
                            const AudioServerPlugInClientInfo*) {
    if (!isDriver(driver)) return kAudioHardwareBadObjectError;
    return device == kObjectID_Device ? kAudioHardwareNoError : kAudioHardwareBadObjectError;
}

// The change action is the new nominal sample rate in Hz.
OSStatus PerformDeviceConfigurationChange(AudioServerPlugInDriverRef driver, AudioObjectID device,
                                          UInt64 action, void*) {
    if (!isDriver(driver)) return kAudioHardwareBadObjectError;
    if (device != kObjectID_Device) return kAudioHardwareBadDeviceError;
    const auto rate = static_cast<Float64>(action);
    if (!spacer::vdev::isSupportedSampleRate(rate)) return kAudioHardwareIllegalOperationError;
    pthread_mutex_lock(&gStateMutex);
    gSampleRate.store(rate);
    updateTimeline(rate);
    gLastRequestedRate = 0.0;
    pthread_mutex_unlock(&gStateMutex);
    if (auto* h = header()) h->sampleRate.store(rate, std::memory_order_relaxed);
    os_log(logger(), "sample rate now %.0f Hz", rate);
    return kAudioHardwareNoError;
}

OSStatus AbortDeviceConfigurationChange(AudioServerPlugInDriverRef driver, AudioObjectID device, UInt64,
                                        void*) {
    if (!isDriver(driver)) return kAudioHardwareBadObjectError;
    if (device != kObjectID_Device) return kAudioHardwareBadDeviceError;
    pthread_mutex_lock(&gStateMutex);
    gLastRequestedRate = 0.0;
    pthread_mutex_unlock(&gStateMutex);
    return kAudioHardwareNoError;
}

// ---- properties ------------------------------------------------------------
bool isOutputOrGlobal(const AudioObjectPropertyAddress* a) {
    return a->mScope == kAudioObjectPropertyScopeGlobal || a->mScope == kAudioObjectPropertyScopeOutput;
}

Boolean HasProperty(AudioServerPlugInDriverRef driver, AudioObjectID object, pid_t,
                    const AudioObjectPropertyAddress* a) {
    if (!isDriver(driver) || a == nullptr) return false;
    switch (object) {
        case kObjectID_PlugIn:
            switch (a->mSelector) {
                case kAudioObjectPropertyBaseClass:
                case kAudioObjectPropertyClass:
                case kAudioObjectPropertyOwner:
                case kAudioObjectPropertyManufacturer:
                case kAudioObjectPropertyOwnedObjects:
                case kAudioPlugInPropertyBoxList:
                case kAudioPlugInPropertyTranslateUIDToBox:
                case kAudioPlugInPropertyDeviceList:
                case kAudioPlugInPropertyTranslateUIDToDevice:
                case kAudioPlugInPropertyResourceBundle:
                    return true;
                default:
                    return false;
            }
        case kObjectID_Device:
            switch (a->mSelector) {
                case kAudioObjectPropertyBaseClass:
                case kAudioObjectPropertyClass:
                case kAudioObjectPropertyOwner:
                case kAudioObjectPropertyName:
                case kAudioObjectPropertyManufacturer:
                case kAudioObjectPropertyOwnedObjects:
                case kAudioDevicePropertyDeviceUID:
                case kAudioDevicePropertyModelUID:
                case kAudioDevicePropertyTransportType:
                case kAudioDevicePropertyRelatedDevices:
                case kAudioDevicePropertyClockDomain:
                case kAudioDevicePropertyDeviceIsAlive:
                case kAudioDevicePropertyDeviceIsRunning:
                case kAudioObjectPropertyControlList:
                case kAudioDevicePropertyNominalSampleRate:
                case kAudioDevicePropertyAvailableNominalSampleRates:
                case kAudioDevicePropertyIsHidden:
                case kAudioDevicePropertyZeroTimeStampPeriod:
                case kAudioDevicePropertyStreams:
                    return true;
                case kAudioDevicePropertyDeviceCanBeDefaultDevice:
                case kAudioDevicePropertyDeviceCanBeDefaultSystemDevice:
                case kAudioDevicePropertyLatency:
                case kAudioDevicePropertySafetyOffset:
                case kAudioDevicePropertyPreferredChannelsForStereo:
                case kAudioDevicePropertyPreferredChannelLayout:
                    return a->mScope == kAudioObjectPropertyScopeOutput ||
                           a->mScope == kAudioObjectPropertyScopeGlobal;
                default:
                    return false;
            }
        case kObjectID_Stream:
            switch (a->mSelector) {
                case kAudioObjectPropertyBaseClass:
                case kAudioObjectPropertyClass:
                case kAudioObjectPropertyOwner:
                case kAudioObjectPropertyName:
                case kAudioObjectPropertyOwnedObjects:
                case kAudioStreamPropertyIsActive:
                case kAudioStreamPropertyDirection:
                case kAudioStreamPropertyTerminalType:
                case kAudioStreamPropertyStartingChannel:
                case kAudioStreamPropertyLatency:
                case kAudioStreamPropertyVirtualFormat:
                case kAudioStreamPropertyPhysicalFormat:
                case kAudioStreamPropertyAvailableVirtualFormats:
                case kAudioStreamPropertyAvailablePhysicalFormats:
                    return true;
                default:
                    return false;
            }
        default:
            return false;
    }
}

OSStatus IsPropertySettable(AudioServerPlugInDriverRef driver, AudioObjectID object, pid_t pid,
                            const AudioObjectPropertyAddress* a, Boolean* out) {
    if (!isDriver(driver) || a == nullptr || out == nullptr) return kAudioHardwareIllegalOperationError;
    if (!HasProperty(driver, object, pid, a)) return kAudioHardwareUnknownPropertyError;
    *out = false;
    if (object == kObjectID_Device && a->mSelector == kAudioDevicePropertyNominalSampleRate) *out = true;
    if (object == kObjectID_Stream &&
        (a->mSelector == kAudioStreamPropertyIsActive || a->mSelector == kAudioStreamPropertyVirtualFormat ||
         a->mSelector == kAudioStreamPropertyPhysicalFormat))
        *out = true;
    return kAudioHardwareNoError;
}

OSStatus GetPropertyDataSize(AudioServerPlugInDriverRef driver, AudioObjectID object, pid_t pid,
                             const AudioObjectPropertyAddress* a, UInt32, const void*, UInt32* out) {
    if (!isDriver(driver) || a == nullptr || out == nullptr) return kAudioHardwareIllegalOperationError;
    if (!HasProperty(driver, object, pid, a)) return kAudioHardwareUnknownPropertyError;
    switch (a->mSelector) {
        case kAudioObjectPropertyBaseClass:
        case kAudioObjectPropertyClass:
            *out = sizeOf<AudioClassID>;
            return kAudioHardwareNoError;
        case kAudioObjectPropertyOwner:
            *out = sizeOf<AudioObjectID>;
            return kAudioHardwareNoError;
        case kAudioObjectPropertyName:
        case kAudioObjectPropertyManufacturer:
        case kAudioDevicePropertyDeviceUID:
        case kAudioDevicePropertyModelUID:
        case kAudioPlugInPropertyResourceBundle:
            *out = sizeOf<CFStringRef>;
            return kAudioHardwareNoError;
        default:
            break;
    }
    switch (object) {
        case kObjectID_PlugIn:
            switch (a->mSelector) {
                case kAudioObjectPropertyOwnedObjects:
                case kAudioPlugInPropertyDeviceList:
                    *out = sizeOf<AudioObjectID>;
                    break;
                case kAudioPlugInPropertyBoxList:
                    *out = 0;
                    break;
                case kAudioPlugInPropertyTranslateUIDToBox:
                case kAudioPlugInPropertyTranslateUIDToDevice:
                    *out = sizeOf<AudioObjectID>;
                    break;
                default:
                    return kAudioHardwareUnknownPropertyError;
            }
            return kAudioHardwareNoError;
        case kObjectID_Device:
            switch (a->mSelector) {
                case kAudioObjectPropertyOwnedObjects:
                case kAudioDevicePropertyStreams:
                    *out = isOutputOrGlobal(a) ? sizeOf<AudioObjectID> : 0;
                    break;
                case kAudioObjectPropertyControlList:
                    *out = 0;
                    break;
                case kAudioDevicePropertyRelatedDevices:
                    *out = sizeOf<AudioObjectID>;
                    break;
                case kAudioDevicePropertyTransportType:
                case kAudioDevicePropertyClockDomain:
                case kAudioDevicePropertyDeviceIsAlive:
                case kAudioDevicePropertyDeviceIsRunning:
                case kAudioDevicePropertyDeviceCanBeDefaultDevice:
                case kAudioDevicePropertyDeviceCanBeDefaultSystemDevice:
                case kAudioDevicePropertyLatency:
                case kAudioDevicePropertySafetyOffset:
                case kAudioDevicePropertyIsHidden:
                case kAudioDevicePropertyZeroTimeStampPeriod:
                    *out = sizeOf<UInt32>;
                    break;
                case kAudioDevicePropertyNominalSampleRate:
                    *out = sizeOf<Float64>;
                    break;
                case kAudioDevicePropertyAvailableNominalSampleRates:
                    *out = kNumSampleRates * sizeOf<AudioValueRange>;
                    break;
                case kAudioDevicePropertyPreferredChannelsForStereo:
                    *out = 2 * sizeOf<UInt32>;
                    break;
                case kAudioDevicePropertyPreferredChannelLayout:
                    *out = channelLayoutSize();
                    break;
                default:
                    return kAudioHardwareUnknownPropertyError;
            }
            return kAudioHardwareNoError;
        case kObjectID_Stream:
            switch (a->mSelector) {
                case kAudioObjectPropertyOwnedObjects:
                    *out = 0;
                    break;
                case kAudioStreamPropertyIsActive:
                case kAudioStreamPropertyDirection:
                case kAudioStreamPropertyTerminalType:
                case kAudioStreamPropertyStartingChannel:
                case kAudioStreamPropertyLatency:
                    *out = sizeOf<UInt32>;
                    break;
                case kAudioStreamPropertyVirtualFormat:
                case kAudioStreamPropertyPhysicalFormat:
                    *out = sizeOf<AudioStreamBasicDescription>;
                    break;
                case kAudioStreamPropertyAvailableVirtualFormats:
                case kAudioStreamPropertyAvailablePhysicalFormats:
                    *out = kNumSampleRates * sizeOf<AudioStreamRangedDescription>;
                    break;
                default:
                    return kAudioHardwareUnknownPropertyError;
            }
            return kAudioHardwareNoError;
        default:
            return kAudioHardwareBadObjectError;
    }
}

template <typename T>
OSStatus put(const T& value, UInt32 capacity, UInt32* outSize, void* out) {
    if (capacity < sizeOf<T>) return kAudioHardwareBadPropertySizeError;
    std::memcpy(out, &value, sizeOf<T>);
    *outSize = sizeOf<T>;
    return kAudioHardwareNoError;
}

OSStatus putIds(const AudioObjectID* ids, UInt32 count, UInt32 capacity, UInt32* outSize, void* out) {
    const UInt32 n = std::min<UInt32>(count, capacity / sizeOf<AudioObjectID>);
    if (n > 0) std::memcpy(out, ids, n * sizeOf<AudioObjectID>);
    *outSize = n * sizeOf<AudioObjectID>;
    return kAudioHardwareNoError;
}

OSStatus GetPropertyData(AudioServerPlugInDriverRef driver, AudioObjectID object, pid_t pid,
                         const AudioObjectPropertyAddress* a, UInt32 qualifierSize, const void* qualifier,
                         UInt32 capacity, UInt32* outSize, void* out) {
    if (!isDriver(driver) || a == nullptr || outSize == nullptr || out == nullptr)
        return kAudioHardwareIllegalOperationError;
    if (!HasProperty(driver, object, pid, a)) return kAudioHardwareUnknownPropertyError;
    const AudioObjectID device = kObjectID_Device;
    const AudioObjectID stream = kObjectID_Stream;

    switch (object) {
        case kObjectID_PlugIn:
            switch (a->mSelector) {
                case kAudioObjectPropertyBaseClass:
                    return put<AudioClassID>(kAudioObjectClassID, capacity, outSize, out);
                case kAudioObjectPropertyClass:
                    return put<AudioClassID>(kAudioPlugInClassID, capacity, outSize, out);
                case kAudioObjectPropertyOwner:
                    return put<AudioObjectID>(kAudioObjectUnknown, capacity, outSize, out);
                case kAudioObjectPropertyManufacturer:
                    return put<CFStringRef>(CFSTR(kManufacturer), capacity, outSize, out);
                case kAudioObjectPropertyOwnedObjects:
                case kAudioPlugInPropertyDeviceList:
                    return putIds(&device, 1, capacity, outSize, out);
                case kAudioPlugInPropertyBoxList:
                    *outSize = 0;
                    return kAudioHardwareNoError;
                case kAudioPlugInPropertyTranslateUIDToBox:
                    return put<AudioObjectID>(kAudioObjectUnknown, capacity, outSize, out);
                case kAudioPlugInPropertyTranslateUIDToDevice: {
                    if (qualifierSize < sizeOf<CFStringRef> || qualifier == nullptr)
                        return kAudioHardwareBadPropertySizeError;
                    CFStringRef uid = *static_cast<const CFStringRef*>(qualifier);
                    const bool match =
                        uid != nullptr && CFStringCompare(uid, CFSTR(kDeviceUID), 0) == kCFCompareEqualTo;
                    return put<AudioObjectID>(match ? kObjectID_Device : kAudioObjectUnknown, capacity,
                                              outSize, out);
                }
                case kAudioPlugInPropertyResourceBundle:
                    return put<CFStringRef>(CFSTR(""), capacity, outSize, out);
                default:
                    return kAudioHardwareUnknownPropertyError;
            }

        case kObjectID_Device:
            switch (a->mSelector) {
                case kAudioObjectPropertyBaseClass:
                    return put<AudioClassID>(kAudioObjectClassID, capacity, outSize, out);
                case kAudioObjectPropertyClass:
                    return put<AudioClassID>(kAudioDeviceClassID, capacity, outSize, out);
                case kAudioObjectPropertyOwner:
                    return put<AudioObjectID>(kObjectID_PlugIn, capacity, outSize, out);
                case kAudioObjectPropertyName:
                    return put<CFStringRef>(CFSTR("Spacer"), capacity, outSize, out);
                case kAudioObjectPropertyManufacturer:
                    return put<CFStringRef>(CFSTR(kManufacturer), capacity, outSize, out);
                case kAudioDevicePropertyDeviceUID:
                    return put<CFStringRef>(CFSTR(kDeviceUID), capacity, outSize, out);
                case kAudioDevicePropertyModelUID:
                    return put<CFStringRef>(CFSTR(kDeviceModelUID), capacity, outSize, out);
                case kAudioDevicePropertyTransportType:
                    return put<UInt32>(kAudioDeviceTransportTypeVirtual, capacity, outSize, out);
                case kAudioDevicePropertyRelatedDevices:
                    return putIds(&device, 1, capacity, outSize, out);
                case kAudioDevicePropertyClockDomain:
                    return put<UInt32>(0, capacity, outSize, out);
                case kAudioDevicePropertyDeviceIsAlive:
                    return put<UInt32>(1, capacity, outSize, out);
                case kAudioDevicePropertyDeviceIsRunning: {
                    pthread_mutex_lock(&gStateMutex);
                    const UInt32 running = gIOCount > 0 ? 1 : 0;
                    pthread_mutex_unlock(&gStateMutex);
                    return put<UInt32>(running, capacity, outSize, out);
                }
                case kAudioDevicePropertyDeviceCanBeDefaultDevice:
                    return put<UInt32>(1, capacity, outSize, out);
                // Keep alert sounds off the virtual device.
                case kAudioDevicePropertyDeviceCanBeDefaultSystemDevice:
                    return put<UInt32>(0, capacity, outSize, out);
                case kAudioDevicePropertyLatency:
                case kAudioDevicePropertySafetyOffset:
                    return put<UInt32>(0, capacity, outSize, out);
                case kAudioObjectPropertyOwnedObjects:
                case kAudioDevicePropertyStreams:
                    return putIds(&stream, isOutputOrGlobal(a) ? 1 : 0, capacity, outSize, out);
                case kAudioObjectPropertyControlList:
                    *outSize = 0;
                    return kAudioHardwareNoError;
                case kAudioDevicePropertyNominalSampleRate:
                    return put<Float64>(gSampleRate.load(), capacity, outSize, out);
                case kAudioDevicePropertyAvailableNominalSampleRates: {
                    const UInt32 n = std::min<UInt32>(kNumSampleRates, capacity / sizeOf<AudioValueRange>);
                    auto* ranges = static_cast<AudioValueRange*>(out);
                    for (UInt32 i = 0; i < n; ++i)
                        ranges[i] = {spacer::vdev::kSupportedSampleRates[i],
                                     spacer::vdev::kSupportedSampleRates[i]};
                    *outSize = n * sizeOf<AudioValueRange>;
                    return kAudioHardwareNoError;
                }
                case kAudioDevicePropertyIsHidden:
                    return put<UInt32>(0, capacity, outSize, out);
                case kAudioDevicePropertyZeroTimeStampPeriod:
                    return put<UInt32>(kZeroTimeStampPeriod, capacity, outSize, out);
                case kAudioDevicePropertyPreferredChannelsForStereo: {
                    if (capacity < 2 * sizeOf<UInt32>) return kAudioHardwareBadPropertySizeError;
                    auto* ch = static_cast<UInt32*>(out);
                    ch[0] = 1;
                    ch[1] = 2;
                    *outSize = 2 * sizeOf<UInt32>;
                    return kAudioHardwareNoError;
                }
                case kAudioDevicePropertyPreferredChannelLayout: {
                    const UInt32 size = channelLayoutSize();
                    if (capacity < size) return kAudioHardwareBadPropertySizeError;
                    auto* layout = static_cast<AudioChannelLayout*>(out);
                    layout->mChannelLayoutTag = kAudioChannelLayoutTag_UseChannelDescriptions;
                    layout->mChannelBitmap = 0;
                    layout->mNumberChannelDescriptions = kNumChannels;
                    for (UInt32 i = 0; i < kNumChannels; ++i) {
                        AudioChannelDescription& d = layout->mChannelDescriptions[i];
                        d.mChannelLabel = kAudioChannelLabel_Discrete_0 | i;
                        d.mChannelFlags = 0;
                        d.mCoordinates[0] = d.mCoordinates[1] = d.mCoordinates[2] = 0.0f;
                    }
                    *outSize = size;
                    return kAudioHardwareNoError;
                }
                default:
                    return kAudioHardwareUnknownPropertyError;
            }

        case kObjectID_Stream:
            switch (a->mSelector) {
                case kAudioObjectPropertyBaseClass:
                    return put<AudioClassID>(kAudioObjectClassID, capacity, outSize, out);
                case kAudioObjectPropertyClass:
                    return put<AudioClassID>(kAudioStreamClassID, capacity, outSize, out);
                case kAudioObjectPropertyOwner:
                    return put<AudioObjectID>(kObjectID_Device, capacity, outSize, out);
                case kAudioObjectPropertyName:
                    return put<CFStringRef>(CFSTR("Spacer 64ch"), capacity, outSize, out);
                case kAudioObjectPropertyOwnedObjects:
                    *outSize = 0;
                    return kAudioHardwareNoError;
                case kAudioStreamPropertyIsActive: {
                    pthread_mutex_lock(&gStateMutex);
                    const UInt32 active = gStreamActive ? 1 : 0;
                    pthread_mutex_unlock(&gStateMutex);
                    return put<UInt32>(active, capacity, outSize, out);
                }
                case kAudioStreamPropertyDirection:
                    return put<UInt32>(0, capacity, outSize, out);  // 0 = output
                case kAudioStreamPropertyTerminalType:
                    return put<UInt32>(kAudioStreamTerminalTypeLine, capacity, outSize, out);
                case kAudioStreamPropertyStartingChannel:
                    return put<UInt32>(1, capacity, outSize, out);
                case kAudioStreamPropertyLatency:
                    return put<UInt32>(0, capacity, outSize, out);
                case kAudioStreamPropertyVirtualFormat:
                case kAudioStreamPropertyPhysicalFormat:
                    return put(streamFormat(gSampleRate.load()), capacity, outSize, out);
                case kAudioStreamPropertyAvailableVirtualFormats:
                case kAudioStreamPropertyAvailablePhysicalFormats: {
                    const UInt32 n =
                        std::min<UInt32>(kNumSampleRates, capacity / sizeOf<AudioStreamRangedDescription>);
                    auto* d = static_cast<AudioStreamRangedDescription*>(out);
                    for (UInt32 i = 0; i < n; ++i) {
                        const Float64 r = spacer::vdev::kSupportedSampleRates[i];
                        d[i].mFormat = streamFormat(r);
                        d[i].mSampleRateRange = {r, r};
                    }
                    *outSize = n * sizeOf<AudioStreamRangedDescription>;
                    return kAudioHardwareNoError;
                }
                default:
                    return kAudioHardwareUnknownPropertyError;
            }
        default:
            return kAudioHardwareBadObjectError;
    }
}

// A client asks for a new nominal rate: done as a configuration change.
OSStatus requestRate(Float64 rate) {
    if (!spacer::vdev::isSupportedSampleRate(rate)) return kAudioHardwareIllegalOperationError;
    if (rate == gSampleRate.load()) return kAudioHardwareNoError;
    if (gHost == nullptr) return kAudioHardwareNotRunningError;
    return gHost->RequestDeviceConfigurationChange(gHost, kObjectID_Device, static_cast<UInt64>(rate),
                                                   nullptr);
}

OSStatus SetPropertyData(AudioServerPlugInDriverRef driver, AudioObjectID object, pid_t pid,
                         const AudioObjectPropertyAddress* a, UInt32, const void*, UInt32 size,
                         const void* data) {
    if (!isDriver(driver) || a == nullptr || data == nullptr) return kAudioHardwareIllegalOperationError;
    if (!HasProperty(driver, object, pid, a)) return kAudioHardwareUnknownPropertyError;
    if (object == kObjectID_Device && a->mSelector == kAudioDevicePropertyNominalSampleRate) {
        if (size < sizeOf<Float64>) return kAudioHardwareBadPropertySizeError;
        return requestRate(*static_cast<const Float64*>(data));
    }
    if (object == kObjectID_Stream) {
        switch (a->mSelector) {
            case kAudioStreamPropertyIsActive: {
                if (size < sizeOf<UInt32>) return kAudioHardwareBadPropertySizeError;
                pthread_mutex_lock(&gStateMutex);
                const bool active = *static_cast<const UInt32*>(data) != 0;
                const bool changed = active != gStreamActive;
                gStreamActive = active;
                pthread_mutex_unlock(&gStateMutex);
                if (changed) notify(kObjectID_Stream, {kAudioStreamPropertyIsActive});
                return kAudioHardwareNoError;
            }
            case kAudioStreamPropertyVirtualFormat:
            case kAudioStreamPropertyPhysicalFormat: {
                if (size < sizeOf<AudioStreamBasicDescription>) return kAudioHardwareBadPropertySizeError;
                const auto& f = *static_cast<const AudioStreamBasicDescription*>(data);
                const auto ours = streamFormat(f.mSampleRate);
                if (f.mFormatID != ours.mFormatID || f.mChannelsPerFrame != ours.mChannelsPerFrame ||
                    f.mBitsPerChannel != ours.mBitsPerChannel || f.mBytesPerFrame != ours.mBytesPerFrame ||
                    (f.mFormatFlags & kAudioFormatFlagIsFloat) == 0)
                    return kAudioDeviceUnsupportedFormatError;
                return requestRate(f.mSampleRate);
            }
            default:
                break;
        }
    }
    return kAudioHardwareUnsupportedOperationError;
}

// ---- IO ----------------------------------------------------------------------
OSStatus StartIO(AudioServerPlugInDriverRef driver, AudioObjectID device, UInt32) {
    if (!isDriver(driver)) return kAudioHardwareBadObjectError;
    if (device != kObjectID_Device) return kAudioHardwareBadDeviceError;
    pthread_mutex_lock(&gStateMutex);
    if (gIOCount == 0) {
        gNumberTimeStamps.store(0);
        gAnchorHostTime.store(mach_absolute_time());
        if (auto* h = header()) h->ioRunning.store(1, std::memory_order_relaxed);
    }
    ++gIOCount;
    pthread_mutex_unlock(&gStateMutex);
    return kAudioHardwareNoError;
}

OSStatus StopIO(AudioServerPlugInDriverRef driver, AudioObjectID device, UInt32) {
    if (!isDriver(driver)) return kAudioHardwareBadObjectError;
    if (device != kObjectID_Device) return kAudioHardwareBadDeviceError;
    pthread_mutex_lock(&gStateMutex);
    if (gIOCount > 0) --gIOCount;
    if (gIOCount == 0)
        if (auto* h = header()) h->ioRunning.store(0, std::memory_order_relaxed);
    pthread_mutex_unlock(&gStateMutex);
    return kAudioHardwareNoError;
}

// The device clock is the host clock at the nominal rate: one zero time stamp
// every kZeroTimeStampPeriod frames since StartIO.
OSStatus GetZeroTimeStamp(AudioServerPlugInDriverRef driver, AudioObjectID device, UInt32,
                          Float64* outSampleTime, UInt64* outHostTime, UInt64* outSeed) {
    if (!isDriver(driver)) return kAudioHardwareBadObjectError;
    if (device != kObjectID_Device) return kAudioHardwareBadDeviceError;
    const Float64 ticksPerPeriod = gHostTicksPerFrame.load() * kZeroTimeStampPeriod;
    const UInt64 anchor = gAnchorHostTime.load();
    UInt64 count = gNumberTimeStamps.load();
    const UInt64 now = mach_absolute_time();
    const auto next = anchor + static_cast<UInt64>(static_cast<Float64>(count + 1) * ticksPerPeriod);
    if (next <= now) gNumberTimeStamps.store(++count);
    *outSampleTime = static_cast<Float64>(count) * kZeroTimeStampPeriod;
    *outHostTime = anchor + static_cast<UInt64>(static_cast<Float64>(count) * ticksPerPeriod);
    *outSeed = 1;
    return kAudioHardwareNoError;
}

OSStatus WillDoIOOperation(AudioServerPlugInDriverRef driver, AudioObjectID device, UInt32, UInt32 operation,
                           Boolean* outWillDo, Boolean* outInPlace) {
    if (!isDriver(driver)) return kAudioHardwareBadObjectError;
    if (device != kObjectID_Device) return kAudioHardwareBadDeviceError;
    const bool writeMix = operation == kAudioServerPlugInIOOperationWriteMix;
    if (outWillDo) *outWillDo = writeMix;
    if (outInPlace) *outInPlace = true;
    return kAudioHardwareNoError;
}

OSStatus BeginIOOperation(AudioServerPlugInDriverRef driver, AudioObjectID device, UInt32, UInt32, UInt32,
                          const AudioServerPlugInIOCycleInfo*) {
    if (!isDriver(driver)) return kAudioHardwareBadObjectError;
    return device == kObjectID_Device ? kAudioHardwareNoError : kAudioHardwareBadDeviceError;
}

// Real-time: lock-free ring write only.
OSStatus DoIOOperation(AudioServerPlugInDriverRef driver, AudioObjectID device, AudioObjectID stream, UInt32,
                       UInt32 operation, UInt32 frames, const AudioServerPlugInIOCycleInfo* cycle,
                       void* mainBuffer, void*) {
    if (!isDriver(driver)) return kAudioHardwareBadObjectError;
    if (device != kObjectID_Device) return kAudioHardwareBadDeviceError;
    if (stream != kObjectID_Stream) return kAudioHardwareBadStreamError;
    if (operation == kAudioServerPlugInIOOperationWriteMix && gWriter.valid()) {
        const auto sampleTime = cycle != nullptr && cycle->mOutputTime.mSampleTime > 0.0
                                    ? static_cast<std::uint64_t>(cycle->mOutputTime.mSampleTime)
                                    : 0;
        gWriter.write(static_cast<const float*>(mainBuffer), frames, sampleTime,
                      spacer::vdev::monotonicNowNs());
    }
    return kAudioHardwareNoError;
}

OSStatus EndIOOperation(AudioServerPlugInDriverRef driver, AudioObjectID device, UInt32, UInt32, UInt32,
                        const AudioServerPlugInIOCycleInfo*) {
    if (!isDriver(driver)) return kAudioHardwareBadObjectError;
    return device == kObjectID_Device ? kAudioHardwareNoError : kAudioHardwareBadDeviceError;
}

}  // namespace

// CFPlugIn factory (Info.plist CFPlugInFactories).
extern "C" __attribute__((visibility("default"))) void* SpacerDriver_Create(CFAllocatorRef,
                                                                            CFUUIDRef requestedType) {
    if (!CFEqual(requestedType, kAudioServerPlugInTypeUUID)) return nullptr;
    return gDriverRef;
}
