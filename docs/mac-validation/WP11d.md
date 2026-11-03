# WP11d validation — Settings dialog + status bar (needs a real Mac)

CI covers the logic behind the UI (Linux and macOS, `spacer_tests "[ui]"`):
sample-rate / buffer-size choices from what a device offers, text-field
parsing and range checks (ports, host, bpm, beats per bar, speed of sound),
which settings change needs what (device reopen, graph rebuild with the new
clock, OSC / MIDI re-bind), the undoable settings edit (apply, undo, redo,
one-drag-one-step, only settings fields touched), clock-source options (Link
only when built) and lock text, the aggregate-device request and its
validation, the app-settings file (virtual device latency, MIDI inputs) and
every status-bar string (CPU, xruns, latency, bpm / bar.beat, external clock
lock, driver status and the install offer). The macOS job also proves the app
(Settings dialog, status bar, composition-root changes) compiles
warning-free.

Everything below needs a device, a listener or another app and is checked by
hand.

## Setup

- Build the app in **Debug** (allocation guard active, SPEC §17):
  `cmake -S . -B build-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build build-debug`.
  Run `build-debug/Spacer_artefacts/Debug/Spacer.app` from a terminal.
- Two output devices if possible (built-in output + a USB interface), for the
  device switch and the aggregate.
- Optional: a MIDI source that sends beat clock / MTC (a DAW with IAC Driver
  enabled), an OSC sender, and the driver from WP5 (install it in check 12).
- Open `resources/examples/stage-demo.spacer` (a sound source helps).

## Checklist

| # | Steps | Expected | Result |
|---|-------|----------|--------|
| 1 | Launch. Look at the bottom of the window. | A status bar: device, `48 kHz / 128`, CPU %, `xruns 0`, `Latency x ms`, `bpm  bar.beat`, driver status. No second status text in the toolbar. | |
| 2 | Play the demo, watch CPU while a cue fires. | CPU follows the load, turns amber at >= 80 %. xruns stay 0 (provoke one by setting a 32-frame buffer with the display asleep and check the counter and amber colour). | |
| 3 | Hover the Latency item. | Tooltip lists device buffer, limiter look-ahead (and the virtual device hop when connected) and the device-reported I/O latency. Headline = buffer + look-ahead (+ hop). | |
| 4 | Toolbar **Settings...** | A modal dialog with Audio, Virtual device, OSC, MIDI, Clock, Installation sections. Esc closes it; reopening shows current values. | |
| 5 | Audio: pick the other output device. | Audio moves to it within a second or two; status line and status bar show the new device; the project (Save) stores the device name. Cmd+Z in the main window (after closing the dialog) goes back to the first device. | |
| 6 | Audio: change sample rate (44.1 / 48 / 88.2 / 96 kHz) and buffer size. | Only values the device offers are listed (plus the project's). Audio continues at the new rate with no pitch change; status bar shows `kHz / buffer`. If a cue list was armed it stays armed (its position restarts: PLAN WP14). | |
| 7 | Audio: enter 330 in Speed of sound, then abc, then 500. | 330 is accepted (speaker delays change slightly, see Layout tab); the others show a red message and the field returns to the old value. | |
| 8 | Audio: **Create aggregate device...** — tick two interfaces, name it, Create. | No error; the new device appears in Audio MIDI Setup and in the device list (reopen the dialog) with the first device as clock. Select it: all channels of both devices are usable in the Layout tab. Remove it in Audio MIDI Setup afterwards. | |
| 9 | Virtual device: set latency 128 / 256 / 512. | Choice persists across relaunch (not in the project file). With the driver connected, the status-bar latency changes by the difference. | |
| 10 | OSC: change input port to 9100; send `/spacer/master/gain -6` there with an OSC tool. | Accepted on 9100, no longer on 9000. Occupy the port with another process (`nc -ul 9101`), set 9101: a yellow bind warning appears in the dialog. | |
| 11 | OSC: enable output, host 127.0.0.1, port 9001; listen with an OSC monitor. | `/spacer/source/*/pos/aed`, meters, `/spacer/clock/beat` arrive at ~30 Hz. An empty host or port 0 is rejected. | |
| 12 | Virtual device: with Spacer.driver absent, the status bar shows an amber warning and an **Install...** button; use **Install virtual device...** (dialog or status bar), enter the admin password. | Installs; the status turns to `Spacer.driver <version>`, "Spacer" appears as an output in other apps; Reinstall label shows afterwards. | |
| 13 | MIDI: untick one input; send a CC mapped via MIDI learn from it. | Ignored; ticking it again works. Selection persists across relaunch. | |
| 14 | MIDI: set Program change channel to 1; send program change 0 / 1 on channel 1. | Scene index 0 / 1 is recalled (0 s). Off ignores it. | |
| 15 | Clock: select **MIDI clock**; start a DAW sending beat clock to the IAC port. | Status bar shows `MIDI clock (Searching...)` then `(Locked)`; bpm / bar.beat follow the DAW; the motions follow. Stop the DAW: the transport stops. | |
| 16 | Clock: **MTC**, then **OSC** (send `/spacer/clock/beat` at 120 bpm), then back to **Internal**. | Lock status per source as in 15; no audio dropout at any switch; Internal resumes with the project tempo and the Tempo field is editable again. | |
| 17 | Clock: Link (only in a build with `-DSPACER_WITH_LINK=ON`). In the normal build select a saved project that uses Link. | Normal build: Link is not in the list; a project with Link shows `Unavailable, running internal` (amber). Link build: joins a Link session, lock follows peers. | |
| 18 | Edit tempo to 90 and beats per bar to 3 (Internal). | Status bar bpm 90.0, bar.beat wraps every 3 beats. Cmd+Z undoes each. Invalid values (10, 0) are rejected. | |
| 19 | Installation mode: tick it, set autosave 5 s, save a project, edit something, wait. | `<name>.autosave.spacer` appears next to the project within ~5 s; quit and relaunch: the last project opens and the show starts. Untick: autosave stops. | |
| 20 | With the dialog open, press Cmd+Z / use the toolbar **Undo** from the main window after closing it, after settings changes. | Dialog values (reopened) match; undoing a clock change returns to the previous source. | |
| 21 | Quit with the dialog open. | The app quits cleanly (no crash, no leak report in Debug). | |
