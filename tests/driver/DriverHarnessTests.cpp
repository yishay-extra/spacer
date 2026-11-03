// macOS only: loads the built Spacer.driver bundle in-process with a fake
// AudioServerPlugIn host (no coreaudiod) and checks its object model, the
// shared-memory handshake and the audio path into VirtualDeviceReceiver.
// Each TEST_CASE runs in its own process (catch_discover_tests), so each
// gets a fresh driver with its own shared-memory name.

#include <CoreAudio/AudioServerPlugIn.h>
#include <CoreFoundation/CoreFoundation.h>
#include <dlfcn.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "engine/VirtualDeviceReceiver.h"
#include "vdev/Protocol.h"
#include "vdev/SharedMemory.h"

#ifndef SPACER_DRIVER_BINARY
#error "SPACER_DRIVER_BINARY must name the driver bundle's executable"
#endif

using namespace spacer;

namespace {

constexpr AudioObjectID kDevice = 2;
constexpr AudioObjectID kStream = 3;

std::atomic<UInt64> gRequestedChange{0};
std::atomic<int> gPropertiesChanged{0};

OSStatus hostPropertiesChanged(AudioServerPlugInHostRef, AudioObjectID, UInt32,
                               const AudioObjectPropertyAddress*) {
    gPropertiesChanged.fetch_add(1);
    return 0;
}
OSStatus hostCopyFromStorage(AudioServerPlugInHostRef, CFStringRef, CFPropertyListRef* out) {
    if (out) *out = nullptr;
    return 0;
}
OSStatus hostWriteToStorage(AudioServerPlugInHostRef, CFStringRef, CFPropertyListRef) {
    return 0;
}
OSStatus hostDeleteFromStorage(AudioServerPlugInHostRef, CFStringRef) {
    return 0;
}
OSStatus hostRequestConfigChange(AudioServerPlugInHostRef, AudioObjectID, UInt64 action, void*) {
    gRequestedChange.store(action);
    return 0;
}

AudioServerPlugInHostInterface gHost = {hostPropertiesChanged, hostCopyFromStorage, hostWriteToStorage,
                                        hostDeleteFromStorage, hostRequestConfigChange};

struct Driver {
    Driver() {
        shmName = "/spacer.d." + std::to_string(::getpid());
        ::setenv("SPACER_DRIVER_SHM_NAME", shmName.c_str(), 1);
        handle = ::dlopen(SPACER_DRIVER_BINARY, RTLD_NOW | RTLD_LOCAL);
        REQUIRE(handle != nullptr);
        using Factory = void* (*)(CFAllocatorRef, CFUUIDRef);
        auto create = reinterpret_cast<Factory>(::dlsym(handle, "SpacerDriver_Create"));
        REQUIRE(create != nullptr);
        ref =
            static_cast<AudioServerPlugInDriverRef>(create(kCFAllocatorDefault, kAudioServerPlugInTypeUUID));
        REQUIRE(ref != nullptr);
        REQUIRE((*ref)->Initialize(ref, &gHost) == 0);
    }
    ~Driver() { vdev::SharedMemory::unlink(shmName.c_str()); }

    AudioServerPlugInDriverInterface& operator*() const { return **ref; }

    template <typename T>
    T get(AudioObjectID object, AudioObjectPropertySelector selector,
          AudioObjectPropertyScope scope = kAudioObjectPropertyScopeGlobal) const {
        const AudioObjectPropertyAddress a{selector, scope, kAudioObjectPropertyElementMain};
        T value{};
        UInt32 size = 0;
        REQUIRE((*ref)->HasProperty(ref, object, 0, &a));
        REQUIRE((*ref)->GetPropertyData(ref, object, 0, &a, 0, nullptr, sizeof(T), &size, &value) == 0);
        REQUIRE(size == sizeof(T));
        return value;
    }

    template <typename T>
    std::vector<T> getArray(AudioObjectID object, AudioObjectPropertySelector selector,
                            AudioObjectPropertyScope scope = kAudioObjectPropertyScopeGlobal) const {
        const AudioObjectPropertyAddress a{selector, scope, kAudioObjectPropertyElementMain};
        UInt32 size = 0;
        REQUIRE((*ref)->GetPropertyDataSize(ref, object, 0, &a, 0, nullptr, &size) == 0);
        std::vector<T> v(size / sizeof(T));
        UInt32 got = 0;
        REQUIRE((*ref)->GetPropertyData(ref, object, 0, &a, 0, nullptr, size, &got, v.data()) == 0);
        REQUIRE(got == size);
        return v;
    }

    std::string getString(AudioObjectID object, AudioObjectPropertySelector selector) const {
        auto s = get<CFStringRef>(object, selector);
        char buffer[256] = {};
        CFStringGetCString(s, buffer, sizeof(buffer), kCFStringEncodingUTF8);
        return buffer;
    }

    std::string shmName;
    void* handle{nullptr};
    AudioServerPlugInDriverRef ref{nullptr};
};

template <typename Pred>
bool waitFor(Pred&& pred, double seconds) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
    while (std::chrono::steady_clock::now() < end) {
        if (pred()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return pred();
}

}  // namespace

TEST_CASE("Spacer.driver: bundle Info.plist registers an AudioServerPlugIn factory") {
    CFURLRef url =
        CFURLCreateFromFileSystemRepresentation(nullptr, reinterpret_cast<const UInt8*>(SPACER_DRIVER_BUNDLE),
                                                std::strlen(SPACER_DRIVER_BUNDLE), true);
    REQUIRE(url != nullptr);
    CFBundleRef bundle = CFBundleCreate(nullptr, url);
    REQUIRE(bundle != nullptr);
    CHECK(CFStringCompare(CFBundleGetIdentifier(bundle), CFSTR("org.spacer.driver"), 0) == kCFCompareEqualTo);
    auto* types =
        static_cast<CFDictionaryRef>(CFBundleGetValueForInfoDictionaryKey(bundle, CFSTR("CFPlugInTypes")));
    REQUIRE(types != nullptr);
    CHECK(CFDictionaryContainsKey(types, CFSTR("443ABAB8-E7B3-491A-B985-BEB9187030DB")));
    auto* version = static_cast<CFStringRef>(
        CFBundleGetValueForInfoDictionaryKey(bundle, CFSTR("CFBundleShortVersionString")));
    REQUIRE(version != nullptr);
    CHECK(CFStringCompare(version, CFSTR(SPACER_VERSION), 0) == kCFCompareEqualTo);
    CFRelease(bundle);
    CFRelease(url);
}

TEST_CASE("Spacer.driver: factory and COM interface") {
    Driver d;
    void* iface = nullptr;
    CHECK((*d).QueryInterface(d.ref, CFUUIDGetUUIDBytes(kAudioServerPlugInDriverInterfaceUUID), &iface) ==
          S_OK);
    CHECK(iface == d.ref);
    CHECK((*d).QueryInterface(d.ref, CFUUIDGetUUIDBytes(kAudioServerPlugInTypeUUID), &iface) ==
          E_NOINTERFACE);
}

TEST_CASE("Spacer.driver: one 'Spacer' device with 64 float32 output channels") {
    Driver d;
    CHECK(d.getArray<AudioObjectID>(kAudioObjectPlugInObject, kAudioPlugInPropertyDeviceList) ==
          std::vector<AudioObjectID>{kDevice});
    CHECK(d.getString(kDevice, kAudioObjectPropertyName) == "Spacer");
    CHECK(d.getString(kDevice, kAudioDevicePropertyDeviceUID) == "org.spacer.driver.device");
    CHECK(d.get<UInt32>(kDevice, kAudioDevicePropertyTransportType) == kAudioDeviceTransportTypeVirtual);
    CHECK(d.get<UInt32>(kDevice, kAudioDevicePropertyDeviceIsAlive) == 1);

    // UID -> device.
    const AudioObjectPropertyAddress tr{kAudioPlugInPropertyTranslateUIDToDevice,
                                        kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    CFStringRef uid = CFSTR("org.spacer.driver.device");
    AudioObjectID found = 0;
    UInt32 size = 0;
    REQUIRE((*d).GetPropertyData(d.ref, kAudioObjectPlugInObject, 0, &tr, sizeof(uid), &uid, sizeof(found),
                                 &size, &found) == 0);
    CHECK(found == kDevice);

    CHECK(d.getArray<AudioObjectID>(kDevice, kAudioDevicePropertyStreams, kAudioObjectPropertyScopeOutput) ==
          std::vector<AudioObjectID>{kStream});
    CHECK(d.getArray<AudioObjectID>(kDevice, kAudioDevicePropertyStreams, kAudioObjectPropertyScopeInput)
              .empty());
    CHECK(d.get<UInt32>(kStream, kAudioStreamPropertyDirection) == 0);  // output

    const auto f = d.get<AudioStreamBasicDescription>(kStream, kAudioStreamPropertyVirtualFormat);
    CHECK(f.mChannelsPerFrame == 64);
    CHECK(f.mBitsPerChannel == 32);
    CHECK((f.mFormatFlags & kAudioFormatFlagIsFloat) != 0);
    CHECK(f.mSampleRate == 48000.0);

    const auto rates = d.getArray<AudioValueRange>(kDevice, kAudioDevicePropertyAvailableNominalSampleRates);
    std::vector<double> mins;
    for (const auto& r : rates) mins.push_back(r.mMinimum);
    CHECK(mins == std::vector<double>{44100.0, 48000.0, 88200.0, 96000.0});
    CHECK(d.getArray<AudioStreamRangedDescription>(kStream, kAudioStreamPropertyAvailablePhysicalFormats)
              .size() == 4);
}

TEST_CASE("Spacer.driver: shared memory carries the version handshake and a heartbeat") {
    Driver d;
    auto m = vdev::SharedMemory::open(d.shmName.c_str());
    REQUIRE(m.valid());
    REQUIRE(vdev::checkHeader(m.data(), m.size()) == vdev::HeaderCheck::Ok);
    auto* h = vdev::headerOf(m.data());
    CHECK(std::string(h->driverVersion) == SPACER_VERSION);
    CHECK(h->sampleRate.load() == 48000.0);
    const auto beat = h->heartbeat.load();
    CHECK(waitFor([&] { return h->heartbeat.load() > beat + 1; }, 2.0));

    VirtualDeviceReceiver r(d.shmName);
    r.poll(0.0);
    CHECK(r.state() == VirtualDeviceReceiver::State::Connected);
    CHECK(r.status().driverVersion == SPACER_VERSION);
}

TEST_CASE("Spacer.driver: follows the sample rate the app requests") {
    Driver d;
    VirtualDeviceReceiver r(d.shmName);
    r.prepare(96000.0, 512);
    r.poll(0.0);
    REQUIRE(r.state() == VirtualDeviceReceiver::State::Connected);
    REQUIRE(waitFor([] { return gRequestedChange.load() == 96000; }, 2.0));
    // The host stops IO and applies the change.
    REQUIRE((*d).PerformDeviceConfigurationChange(d.ref, kDevice, 96000, nullptr) == 0);
    CHECK(d.get<Float64>(kDevice, kAudioDevicePropertyNominalSampleRate) == 96000.0);
    CHECK(d.get<AudioStreamBasicDescription>(kStream, kAudioStreamPropertyVirtualFormat).mSampleRate ==
          96000.0);
    CHECK(r.status().driverSampleRate == 96000.0);
    // Unsupported rates are refused.
    CHECK((*d).PerformDeviceConfigurationChange(d.ref, kDevice, 22050, nullptr) != 0);
}

TEST_CASE("Spacer.driver: a client's sine on channel 7 arrives on virtual channel 7") {
    Driver d;
    VirtualDeviceReceiver r(d.shmName);
    constexpr int frames = 512;
    r.prepare(48000.0, frames);
    r.poll(0.0);
    REQUIRE(r.state() == VirtualDeviceReceiver::State::Connected);

    REQUIRE((*d).StartIO(d.ref, kDevice, 1) == 0);
    CHECK(d.get<UInt32>(kDevice, kAudioDevicePropertyDeviceIsRunning) == 1);
    CHECK(r.status().driverRunning);

    Boolean willDo = false, inPlace = false;
    REQUIRE((*d).WillDoIOOperation(d.ref, kDevice, 1, kAudioServerPlugInIOOperationWriteMix, &willDo,
                                   &inPlace) == 0);
    CHECK(willDo);
    REQUIRE((*d).WillDoIOOperation(d.ref, kDevice, 1, kAudioServerPlugInIOOperationReadInput, &willDo,
                                   &inPlace) == 0);
    CHECK_FALSE(willDo);

    Float64 sampleTime = -1.0;
    UInt64 hostTime = 0, seed = 0;
    REQUIRE((*d).GetZeroTimeStamp(d.ref, kDevice, 1, &sampleTime, &hostTime, &seed) == 0);
    CHECK(sampleTime == 0.0);

    std::vector<float> buffer(std::size_t{frames} * 64);
    std::vector<float> ch7, others;
    AudioServerPlugInIOCycleInfo cycle{};
    std::uint64_t n = 0;
    for (int block = 0; block < 100; ++block) {
        std::fill(buffer.begin(), buffer.end(), 0.0f);
        for (int i = 0; i < frames; ++i, ++n)
            buffer[static_cast<std::size_t>(i) * 64 + 7] =
                static_cast<float>(0.5 * std::sin(2.0 * M_PI * 1000.0 * static_cast<double>(n) / 48000.0));
        cycle.mOutputTime.mSampleTime = static_cast<Float64>(block * frames);
        REQUIRE((*d).BeginIOOperation(d.ref, kDevice, 1, kAudioServerPlugInIOOperationWriteMix, frames,
                                      &cycle) == 0);
        REQUIRE((*d).DoIOOperation(d.ref, kDevice, kStream, 1, kAudioServerPlugInIOOperationWriteMix, frames,
                                   &cycle, buffer.data(), nullptr) == 0);
        REQUIRE((*d).EndIOOperation(d.ref, kDevice, 1, kAudioServerPlugInIOOperationWriteMix, frames,
                                    &cycle) == 0);
        r.pull(frames);
        ch7.insert(ch7.end(), r.channels()[7], r.channels()[7] + frames);
        others.insert(others.end(), r.channels()[6], r.channels()[6] + frames);
        others.insert(others.end(), r.channels()[8], r.channels()[8] + frames);
    }
    REQUIRE(r.status().playing);
    double sum = 0.0;
    for (std::size_t i = ch7.size() - 24000; i < ch7.size(); ++i) sum += double(ch7[i]) * ch7[i];
    CHECK(std::sqrt(sum / 24000.0) == Catch::Approx(0.5 / std::sqrt(2.0)).margin(0.02));
    float peakOthers = 0.0f;
    for (float x : others) peakOthers = std::max(peakOthers, std::abs(x));
    CHECK(peakOthers == 0.0f);

    REQUIRE((*d).StopIO(d.ref, kDevice, 1) == 0);
    CHECK(d.get<UInt32>(kDevice, kAudioDevicePropertyDeviceIsRunning) == 0);
    CHECK_FALSE(r.status().driverRunning);
}
