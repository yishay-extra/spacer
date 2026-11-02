# WP3 — manual validation on a real Mac

CI builds and runs everything that needs no audio hardware. That covers the
offline-render acceptance tests, the limiter, the command FIFO, the
allocation guard in tests, and the §17 benchmark, which runs on the macOS
runner. This checklist covers what CI cannot: real device I/O, the
CoreAudio aggregate helper, and listening.

Record a result (pass / fail plus notes) for each numbered check.

## Setup

1. A Mac on macOS 12 or later, with headphones or two speakers on the built-in output or an interface.
   For check 7 you also need a second audio device (any USB interface; a Bluetooth or AirPlay output also works).
2. Build in Release, then once more in Debug for check 8:
   ```
   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
   cmake -S . -B build-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build build-debug
   ```
   Or download the `Spacer-macOS` and `spacer-engine-check-macOS` artifacts
   from the PR's macOS CI job.
   The tool is the `spacer-engine-check` binary under
   `build/spacer_engine_check_artefacts/`. Below it is called `check`:
   ```
   check() { "$(find build/spacer_engine_check_artefacts -type f -name 'spacer-engine-check' | head -1)" "$@"; }
   ```
   If you used the CI artifact, unzip it and run `chmod +x` on the binary first.
3. The first run asks for microphone permission; allow it.
4. Set the output volume low. Every tone is a 440 Hz sine at −20 dBFS.

`check` prints one status line per second:
`t=… device rate / buffer out=N latency=device+graph smp cpu=…% xruns=N dropped=N`.

**Global pass rule:** for every check, `xruns` stays 0 and `dropped` stays 0.
The only exception is check 6, which deliberately disturbs the device.

## Checks

| # | Do | Expected | Pass if |
|---|----|----------|---------|
| 1 | `check devices` | Every CoreAudio device is listed with its name, UID, and input / output channel counts. | The built-in output and any interface appear, and the channel counts match Audio MIDI Setup. |
| 2 | `check tone --seconds 10` (default device, az −90) | A steady tone in the **left** ear/speaker only. | Nothing at all from the right. No clicks after the start; the start is a short fade-in. |
| 3 | `check tone --az 90 --seconds 10` | The tone is in the **right** only. | Nothing from the left. |
| 4 | `check pan --seconds 20` | The tone sweeps smoothly L → R → L, one cycle every 4 s. Mid-sweep it is centred and about equally loud: constant power, with no dip or bump. | No zipper noise, steps or clicks while it moves. `dropped` = 0. |
| 5 | `check delay --seconds 10` | The same tone on L and R. L is quieter (−6 dB) and delayed by 2.915 ms (L is 1 m, R is 2 m). | The image leans right (the louder side). Optionally loop the outputs back into a recorder (e.g. BlackHole + Audacity) and measure: L lags R by 140 samples at 48 kHz (±1), and L is 6.0 dB down (±0.1). |
| 6 | Run `check tone --seconds 60`. Meanwhile, in Audio MIDI Setup, change the output's sample rate (48 k → 44.1 k → 96 k), then unplug the interface (if you are using one). | After each rate change the status line shows the new rate and the tone continues at the same pitch. After an unplug, the tool keeps running without crashing. | No crash or hang. The pitch stays 440 Hz at every rate. A short glitch at the moment of change is acceptable. |
| 7 | `check aggregate-create "Spacer Test" org.spacer.test <UID-A> <UID-B>` with two UIDs from check 1. Then run `check devices`, then `check tone --device "Spacer Test" --seconds 10`, then `check aggregate-destroy org.spacer.test`, then `check devices` again. | It prints `created aggregate device id N`. "Spacer Test" is listed with in/out = the sum of both devices, and also appears in Audio MIDI Setup. The tone plays on its first two outputs (device A's channels 1–2). Destroy prints `destroyed`, after which the device is gone from both lists. | Every step behaves as described; nothing is left over in Audio MIDI Setup. |
| 8 | Debug build: the `spacer-engine-check` under `build-debug/spacer_engine_check_artefacts/`, run as `… pan --seconds 30`. Also launch `build-debug/Spacer_artefacts/Debug/Spacer.app` and leave it open for 1 min. | Same behaviour as the Release build. The allocation guard is active (the tool has it in every build, the app only in Debug): if the audio callback allocates or frees, the process prints `spacer: operator new on the audio thread (allocation guard)` and aborts. | The process runs to the end and the app stays open; that message never appears. |
| 9 | Launch `Spacer.app` (Release). | The window shows "Spacer", with a status line below it: device name, rate / buffer, `2 out` or more, latency, CPU and xruns. The output is silent, because live sources only arrive in WP4. | The status line is shown and no error is shown. CPU is well under 5 %. Xruns stay at 0 for 1 min. |
| 10 | `check tone --rate 48000 --buffer 128 --seconds 10`, then the same with `--buffer 32` and `--buffer 2048`. | The status line shows the requested buffer size, where the device supports it. | The tone is clean at every buffer size. Xruns stay at 0 at 128 and 2048; at 32, a few at start-up are acceptable. Graph latency reads 72 smp at 48 kHz (the limiter's 1.5 ms). |

## What is not checked here

- Live inputs (hardware, file, virtual device): WP4 and WP5.
- Motion: WP7.
- DBAP and ambisonics: WP2c and WP2d. Until then they fall back to VBAP.
- UI: WP11.
