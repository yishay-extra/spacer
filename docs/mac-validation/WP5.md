# WP5 — manual validation on a real Mac (Spacer.driver)

CI (Linux + macOS) builds and tests everything that needs neither coreaudiod
nor a listener:

- the shared-memory protocol (`driver/protocol`): layout, versioning, wrap
  around, overrun counting, threads, and two processes over POSIX shm;
- `VirtualDeviceReceiver` against a simulated driver: connect / absent /
  incompatible / not responding / driver restart, ±1000 ppm drift with no
  underruns, a 44.1 kHz driver resampled to 48 kHz, underrun counting and
  re-priming, backlog resync, no allocation in `pull()`;
- the `Spacer.driver` bundle itself (macOS job): Info.plist, the exported
  factory, system-only linkage, and the plug-in **loaded in-process with a
  fake AudioServerPlugIn host** — 1 device "Spacer", 64 float32 output
  channels, 44.1/48/88.2/96 kHz, the version handshake and heartbeat in
  shared memory, the app's sample-rate request reaching the host, and a sine
  written by `DoIOOperation(WriteMix)` on channel 7 arriving on
  `VirtualDeviceReceiver` channel 7 only.

This checklist covers what CI cannot: coreaudiod actually loading the
bundle, a real sender app, the admin-prompt installer, and listening.
Record pass / fail plus notes for each check.

## Setup

1. A Mac on macOS 12 or later, headphones or speakers on the built-in output.
2. Build (Release): `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build`,
   or download the `Spacer-macOS`, `Spacer-driver-macOS` and
   `spacer-engine-check-macOS` artifacts from the PR's macOS CI job (unzip;
   `chmod +x` the tool; `xattr -dr com.apple.quarantine` on the bundles).
3. The tool, called `check` below:
   ```
   check() { "$(find build/spacer_engine_check_artefacts -type f -name 'spacer-engine-check' | head -1)" "$@"; }
   ```
4. The bundles are ad-hoc signed only (signing / notarisation is WP13). If
   coreaudiod refuses to load an ad-hoc-signed plug-in on your macOS
   version, note it in check 2 and continue with
   `sudo codesign --force --deep --sign - /Library/Audio/Plug-Ins/HAL/Spacer.driver`
   followed by `sudo killall coreaudiod`.
5. Volume low; tones are 440 Hz at −20 dBFS.

Driver logs: `log stream --predicate 'subsystem == "org.spacer.driver"'`.

## Checks

| # | Do | Expected | Pass if |
|---|----|----------|---------|
| 1 | **Driver absent.** Make sure `/Library/Audio/Plug-Ins/HAL/Spacer.driver` does not exist (`sudo rm -rf` it and `sudo killall coreaudiod` if it does). Start Spacer.app. | App starts and plays as usual. Status bar ends with "Spacer virtual device not found (driver not installed or not loaded)" and an **Install virtual device...** button is shown. `check virtual --channel 6 --seconds 5` prints `driver=absent` every second and exits normally. | No crash, no dialog, warning visible |
| 2 | **Install.** Click **Install virtual device...**, enter the admin password. | An "installed" dialog with the version (e.g. 0.1.0). `ls /Library/Audio/Plug-Ins/HAL/Spacer.driver/Contents/MacOS/Spacer` exists, owner root:wheel. Within a few seconds the status bar shows `Spacer.driver 0.1.0` and the button disappears. | All of it |
| 3 | **Install cancelled.** Remove the driver again (check 1), click Install and press Cancel at the password prompt. | A "Could not install" dialog; app keeps running. Reinstall afterwards (check 2). | No crash |
| 4 | **Acceptance: device listed.** `system_profiler SPAudioDataType` | Lists a device **"Spacer"**, manufacturer Spacer, **Output Channels: 64**, transport Virtual. Audio MIDI Setup shows "Spacer" with 64 outputs and rates 44.1/48/88.2/96 kHz. | Name and 64 channels |
| 5 | **Acceptance: channel 7.** Terminal A: `check virtual --channel 6 --seconds 30` (0-based 6 = channel 7). Terminal B: `check vsend --channel 6 --seconds 20 --buffer 512`. | Terminal A goes `driver=connected running=1 playing=1`; the tone is heard on the **left** speaker for the 20 s of B and stops when B stops. `underruns` stays 0 while B runs. | Tone heard, underruns 0 |
| 6 | **Channel isolation.** Repeat 5 with B on `--channel 5` and `--channel 7` (A still on 6). | Silence in both cases. | Silence |
| 7 | **Latency (one driver buffer).** Repeat 5 with B `--buffer 128`, `256`, `512`. In A, note `fill` and `latency` (512 by default). | `fill` stays within about `latency + block + sender buffer` frames and does not grow over 30 s. The tone starts within ~20 ms of starting B (perceptually immediate). | fill bounded, starts promptly |
| 8 | **Sample-rate following.** Run A with `--rate 44100`, then with `--rate 96000`, with B running each time. | Audio MIDI Setup shows "Spacer" switching to the engine's rate within ~1 s; A shows `rate=44100` / `rate=96000`; tone continues at the same pitch (440 Hz). | Rate follows, pitch right |
| 9 | **Rate mismatch.** With A at 48000 and Spacer forced to 44100 in Audio MIDI Setup *while A is not running*, start B then A. | Before A's request lands, `ratio` ≈ 0.919 and the tone is still at 440 Hz (resampled); then the rate switches (check 8). | No pitch error |
| 10 | **Drift (long run).** A and B at 48 kHz, A on the built-in output, 10 minutes. | `ratio` settles near 1 (within ±0.0005); `underruns` 0, `resyncs` 0, no clicks heard. | All of it |
| 11 | **DAW.** Pick "Spacer" as output in a DAW (Logic, Reaper, Ableton), route a track to output 7. In Spacer.app add a VirtualInput source on channel 6 (0-based). | Track is heard through Spacer at the source's position. | Heard |
| 12 | **coreaudiod restart.** With A and B running: `sudo killall coreaudiod`. | B stops (its device went away); A shows `not-responding` then `connected` again with a new session; after restarting B the tone returns. A never crashes. | Recovers |
| 13 | **Driver removed while running.** With A running: `sudo rm -rf /Library/Audio/Plug-Ins/HAL/Spacer.driver && sudo killall coreaudiod`. | A goes to `absent` within ~5 s and keeps running; Spacer.app shows the warning and the Install button. | No crash |
| 14 | **Incompatible driver (optional).** Not testable without a second protocol version; covered by `VirtualDevice: an incompatible protocol is refused and reported` in CI. | — | — |
