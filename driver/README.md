# Spacer.driver — virtual audio device

CoreAudio **AudioServerPlugIn** (SPEC §14) that presents one output device,
**"Spacer"**, with 64 float32 channels at 44.1 / 48 / 88.2 / 96 kHz, to other
applications, and hands their audio to the Spacer app through POSIX shared
memory. Bundle id `org.spacer.driver`, installed to
`/Library/Audio/Plug-Ins/HAL/Spacer.driver` (the app's "Install virtual
device..." action does that behind the admin prompt).

```
driver/
  protocol/vdev/   header-only shared-memory protocol, also used by the app
                   (src/engine/VirtualDeviceReceiver) and tested on Linux:
    Protocol.h       layout, constants, versioning, header check / init
    RingWriter.h     producer (driver IO thread)
    RingReader.h     consumer (app audio thread)
    SharedMemory.h   POSIX shm create / open / map
  src/SpacerDriver.cpp   the plug-in (macOS only)
  Info.plist.in          CFPlugIn factory registration
  TESTING.md             how it is tested
```

Dependency rule: no JUCE, no `spacer_core`; the plug-in includes only the
protocol headers. The CMake target `SpacerDriver` (macOS) builds
`build/driver/Spacer.driver`; the app target embeds a copy in
`Spacer.app/Contents/Resources/`.

## How it works

- **Device clock**: host clock (`mach_absolute_time`) at the nominal rate;
  zero time stamps every 16384 frames.
- **IO**: only `WriteMix` is handled; each cycle's mixed client output (64
  interleaved channels) is appended to the ring. When the app is not reading
  and the ring is full, new frames are dropped and counted (`overruns`).
- **Shared memory** `/org.spacer.driver`: created (mode 0666) when the
  plug-in is initialised; a page-sized header then a 16384-frame × 64-channel
  float ring. Header: magic, protocol version, geometry, session id, driver
  version, write index (sequence counter), sample rate, IO-running flag,
  last write size / sample time / host time, heartbeat (every 250 ms), and
  the app's read index, underrun counter, requested sample rate and protocol
  version.
- **Sample rate**: follows the rate the app writes into
  `requestedSampleRate` (polled with the heartbeat; applied through
  `RequestDeviceConfigurationChange`). Clients can also set it.
- **Drift**: handled in the app (fill-level-steered resampler,
  `VirtualDeviceReceiver`).
