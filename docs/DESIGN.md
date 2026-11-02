# Spacer — Design Document

> Status: **Draft v2** · Last updated: 2026-11-02

Spacer is a macOS desktop application for **spatial audio over arbitrary
speaker arrays**. It receives audio from DAWs and other applications (via a
virtual audio device) and from audio files, places each source in space over
2–32 speakers using standard multichannel panning algorithms, and lets the
positions be manipulated live, automated, and synced to a clock. It is built
for live performance and for static installations.

---

## 1. Goals

1. **Take audio from anywhere** — any app that can select an output device
   (DAWs, players, browsers) can send to Spacer; Spacer also plays audio files.
2. **Arbitrary speaker layouts** — 2 to 32 outputs, any physical arrangement
   (rings, domes, lines, irregular rooms), described by real-world positions.
3. **Spatialization that respects the room** — distance/delay/gain
   compensation per speaker, plus established panning algorithms (VBAP, DBAP,
   ambisonics) so a source "lives" at a point in space rather than on a channel.
4. **Motion** — source positions are controllable in real time (mouse, OSC,
   MIDI), driven by programmed trajectories (orbits, paths, random walks), or
   stepped between positions on a time grid, all synced to a clock.
5. **Live-performance ready** — scenes and cues are recallable instantly and
   glitch-free; nothing the UI does can stall the audio.
6. **Installation ready** — the whole state (layout, sources, motion, cues)
   is saved in a project file; programs can loop forever, with programmed
   variation, unattended.
7. **Low latency** — target ≤ 10 ms added latency at 48 kHz / 128 frames,
   excluding the virtual-device hop.

## 2. Non-goals (phase 1)

- Binaural / headphone rendering, and ambisonic or object-based file export.
  (Designed for; not built.)
- Windows / Linux. The engine stays portable (JUCE), the device layer is
  macOS-only for now.
- Being a DAW: no multitrack recording or editing timeline. Spacer's "time" is
  cues, loops and clocks, not a tape.
- Network audio (Dante/AES67/AVB): use a CoreAudio driver for those; Spacer
  sees them as a normal device.
- Room measurement/correction (FIR). Manual per-speaker trim/delay/EQ only.

## 3. Technology

**C++20 with JUCE** (AGPLv3/commercial dual licence, no royalties — see §11).

Why C++ over Rust: CoreAudio, AudioUnit hosting, and the HAL virtual-device
plugin API are C/Objective-C; spatial DSP libraries and reference
implementations (VBAP, ambisonics decoders, SOFA) are C++; JUCE gives us
audio I/O, AU hosting, a GUI toolkit, OSC and MIDI in one place; and the
audio-developer community is C++. Rust was considered for memory safety but
would cost ecosystem on every front that matters here.

Supporting pieces:

| Need | Choice |
|------|--------|
| Audio I/O, device enumeration | JUCE `AudioDeviceManager` over CoreAudio; aggregate devices for multi-interface rigs |
| Virtual input device | CoreAudio **AudioServerPlugIn** (user-space HAL driver, same mechanism as BlackHole/Loopback), shipped as `Spacer.driver` |
| File decoding | JUCE audio formats (WAV, AIFF, FLAC, OGG) + CoreAudio/AVFoundation for MP3/AAC/ALAC |
| Spatial DSP | In-house VBAP/DBAP; ambisonics via an in-house encoder + AllRAD-style decoder (reference: IEM / SPARTA designs) |
| Plugin hosting | JUCE `AudioPluginFormatManager` — AudioUnit (AUv2) in phase 1 |
| Control in | JUCE OSC, JUCE MIDI |
| Sync | Internal clock first; Ableton Link SDK, MIDI Clock/MTC, OSC timecode later |
| Project files | JSON (`.spacer`), human-readable, versioned schema |
| Build / CI | CMake, GitHub Actions (macOS), notarized DMG |

## 4. Core concepts

| Concept | Description |
|---------|-------------|
| **Speaker layout** | The physical array: N speakers, each with position (azimuth, elevation, distance, or x/y/z in metres), trim gain, delay, and optional EQ. Plus the listening-position origin. Saved as reusable presets. |
| **Output device** | The CoreAudio device (or aggregate) whose channels the layout maps to. Layout ↔ channel mapping is explicit. |
| **Source** | A mono or stereo audio stream with a position in space. Kinds: *input source* (channels of the Spacer virtual device or a hardware input), *file source* (player with transport), *generator* (test tones, noise). |
| **Position** | Where a source is, in the same coordinates as the layout. For stereo sources, two positions linked by a width/angle. |
| **Spatializer** | The algorithm turning (source signal, position, layout) into N speaker signals: VBAP, DBAP, or Ambisonics (encode → decode). Selectable per source. |
| **Motion** | What drives a position over time: *manual*, *trajectory* (orbit, line, path, random walk, Lissajous), or *step sequence* (list of positions with durations). |
| **Clock** | The time reference motions and cues sync to: tempo + transport. Internal, Ableton Link, MIDI Clock/MTC, OSC. |
| **Scene** | A complete snapshot of the mutable state: sources, positions, motions, levels, plugin parameters. |
| **Cue** | An instruction to go to a scene (or apply a partial change) with a transition time, triggered manually, by clock position, by OSC/MIDI, or by the previous cue finishing. |
| **Cue list / Program** | An ordered list of cues; can loop. The unit of a performance or an installation run. |
| **Project** | Layout + device mapping + sources + scenes + cue lists + I/O mappings. One `.spacer` file. |

## 5. Architecture

```
 ┌──────────────────────────────────────────────────────────────────────────────┐
 │ Senders                                                                      │
 │   DAW / any app ──▶ "Spacer" virtual device (HAL plugin, up to 64 ch)        │
 │   Audio files    ──▶ file players (disk thread + ring buffers)               │
 │   Hardware inputs──▶ CoreAudio input channels                                │
 └───────────────┬──────────────────────────────────────────────────────────────┘
                 ▼
 ┌──────────────────────────────── ENGINE (RT) ─────────────────────────────────┐
 │  Audio callback (one, on the output device / aggregate):                     │
 │                                                                              │
 │   per source:  input ─▶ [AU inserts] ─▶ gain ─▶ SPATIALIZER ─┐               │
 │                                   ▲                          │ N ch          │
 │                       position ◀──┘ (from Motion engine)     ▼               │
 │                                                       Σ speaker bus (N)      │
 │                                                              │               │
 │   per speaker: delay/gain compensation ─▶ trim/EQ ─▶ [AU inserts] ─▶ limiter │
 │                                                              ▼               │
 │                                                    CoreAudio output (N ch)   │
 │                                                                              │
 │  Motion engine (RT, sample-accurate): clock ─▶ trajectories/steps ─▶ positions│
 │  Cue engine (RT-safe): scheduled transitions ─▶ parameter ramps              │
 └────────────▲──────────────────────────────────────────▲──────────────────────┘
              │ commands (lock-free queues)               │ meters, positions, clock
              │                                           ▼ (lock-free, polled)
 ┌────────────┴────────────────── CONTROL (non-RT) ─────────────────────────────┐
 │  Project model (single source of truth, undoable)                            │
 │  ├─ Scene/cue manager          ├─ Clock sources: internal · Link · MIDI · OSC│
 │  ├─ OSC server / MIDI mapping  ├─ Plugin scanner / editor windows            │
 │  └─ Project file load/save     └─ Device manager (CoreAudio, aggregates)     │
 │                                                                              │
 │  UI (JUCE): stage view (top/side), layout editor, source strips, cue list,  │
 │             motion editor, meters, device/IO settings                        │
 └──────────────────────────────────────────────────────────────────────────────┘
```

### 5.1 Engine (real-time)

- A **single audio callback** on the output device. Multi-interface rigs are
  handled by a CoreAudio **aggregate device** (macOS does clock drift
  correction), which Spacer can create and manage for the user.
- Virtual-device input arrives via the HAL plugin's shared-memory ring; file
  sources are pre-buffered by a disk thread. Both are read lock-free in the
  callback.
- **Signal path per source:** input → AU inserts → gain → spatializer →
  speaker bus. **Per speaker:** compensation delay + gain → trim/EQ → AU
  inserts → limiter → device.
- **Real-time rules:** no allocation, no locks, no I/O, no plugin scanning in
  the callback. Enforced by code review and a debug-build allocation guard.
- **Parameter changes** (gain, position, plugin params) arrive through
  lock-free queues and are **ramped** (default 10 ms, cue transitions can be
  seconds) to stay click-free.
- **Structural changes** (add/remove source, change layout, insert plugin)
  are prepared on the control thread, then swapped in atomically. Removed
  sources fade out over one buffer before removal.

### 5.2 Spatializers

All take a position in the layout's coordinates and produce N gains (or, for
ambisonics, an encode + fixed decode matrix). All are computed per block and
interpolated per sample so moving sources don't zipper.

| Algorithm | Use when | Notes |
|-----------|----------|-------|
| **VBAP** (2D/3D) | Rings, domes, any layout with a well-defined hull | Standard for arbitrary layouts; sharp localisation; needs the listener near the centre |
| **DBAP** | Irregular layouts, installations where there is no sweet spot | Distance-based, works with speakers anywhere; smoother, less precise; has a rolloff parameter |
| **Ambisonics** (1st–5th order, AllRAD decode) | Smooth motion, many simultaneous sources, importing ambisonic material | Encode is cheap per source; one decode for all; order limited by speaker count |
| **Direct / nearest** | Utility: send to one speaker or a fixed set | For stems that should stay on a channel |

**Source distance** is a parameter too: gain falloff (1/r with a configurable
exponent and minimum radius), optional low-pass with distance, and for VBAP a
spread parameter that widens the source as it comes closer/inside the array.

### 5.3 Speaker layout and room compensation

- Layouts are edited visually (top and side views) or numerically; presets
  for stereo, quad, 5.1, 7.1, 7.1.4, octagon, hexadecagon, dome 22, etc.
- **Automatic compensation** from positions: delay so all speakers' wavefronts
  arrive at the listening position together, gain so each contributes equally
  (1/r). Both can be overridden per speaker with measured values.
- Per-speaker **trim, delay, polarity, 3-band EQ**, and a **limiter** on every
  output (speaker protection is non-negotiable in live use).
- Layout → device channel mapping is explicit and saved with the project.

### 5.4 Motion engine

Each source position is driven by one *motion*:

- **Manual** — last value set by UI/OSC/MIDI; smoothing time configurable.
- **Trajectory** — parametric: *orbit* (centre, radius, speed in rev/bar or
  rev/s, direction, elevation), *line/bounce*, *Lissajous*, *custom path*
  (user-drawn polyline/spline, constant speed or time-stamped), *random walk*
  (bounds, step size, seed). Speed can be expressed in clock units (bars,
  beats) or seconds.
- **Step sequence** — an ordered list of `(position | speaker, duration,
  transition)`; duration in beats or seconds; transition is jump or glide.
  Loop, ping-pong, or random order; optional probability per step for
  programmed variation.
- **Modulation** — any motion parameter can be offset by an LFO or by an
  OSC/MIDI input (e.g. radius from a fader).

Motions are evaluated in the audio callback against the active clock, so
timing is sample-accurate and survives UI stalls. Phase is derived from the
clock's beat position, so a Link/MIDI-clock tempo change keeps everything
locked.

### 5.5 Clock

A `Clock` interface provides `(tempo, beat position, is_playing)` per block.

- **Internal** (phase 1): tempo, play/stop, tap tempo, optional quantised
  start.
- **Ableton Link**: shared tempo/beat/phase with other apps on the network.
- **MIDI Clock** and **MTC**: follow a DAW or hardware.
- **OSC**: explicit beat/time messages (e.g. from Max/TouchDesigner).

Only one clock is active; switching is a control-thread operation. Cues and
motions reference beat position, so they are independent of which clock drives
it.

### 5.6 Scenes, cues and programs

- A **scene** stores every mutable parameter. Recall applies all changes as
  ramps with a per-cue transition time (0 ms → minutes), including positions
  and motion settings. Motions can be told to restart phase or continue.
- A **cue** = target scene (or a partial change) + transition + trigger.
  Triggers: manual (Go button / key), at beat position, after previous cue +
  delay, OSC address, MIDI note/CC, time of day (installations).
- A **cue list** is ordered, can loop, and supports **follow-on** chains, so an
  installation is a looping cue list with randomised durations and step
  sequences for variation. A performance is a cue list driven by Go.
- Everything is undoable in the editor and saved in the project.

### 5.7 Control inputs (OSC / MIDI)

- OSC server (UDP, configurable port) with a documented address space, e.g.
  `/spacer/source/{id}/pos/aed azimuth elevation distance`,
  `/spacer/source/{id}/gain`, `/spacer/cue/go`, `/spacer/scene/recall name`,
  `/spacer/motion/{id}/speed`. Also OSC *out* for positions and meters.
- MIDI learn on any parameter; MIDI notes trigger cues; MPE/14-bit CC
  supported for smooth position control.
- Both feed the same command queue as the UI; no special paths.

### 5.8 Plugin hosting

- AudioUnit (AUv2; AUv3 as follow-up) inserts on each source (pre-spatializer)
  and on each speaker output (post-compensation). Plugin state is part of
  scenes; parameters are automatable from cues and modulators.
- Scanning happens out of process (JUCE's sandboxed scanner) so a crashing
  plugin can't take Spacer down during setup; at runtime plugins are in
  process for latency reasons.
- Plugin latency is reported and compensated per source/speaker chain.

### 5.9 Virtual audio device (`Spacer.driver`)

- AudioServerPlugIn with up to 64 input channels, presenting as output device
  "Spacer" to other apps at the engine's sample rate.
- Transfers audio to the app through shared memory + a lock-free ring; adds
  one device buffer of latency.
- Installed into `/Library/Audio/Plug-Ins/HAL/` by the app's installer; needs
  a `coreaudiod` restart on first install. Signed and notarized.
- If the driver is absent, Spacer still works with hardware inputs and files.

## 6. User interface (phase 1)

- **Stage view** — top-down (and side) view of the layout with sources as
  draggable objects, trajectories drawn, live meters on speakers. This is the
  main performance surface.
- **Layout editor** — place speakers, see computed delays/gains, test each
  with pink noise, assign device channels.
- **Source strips** — per source: input selection, gain, spatializer choice,
  distance params, motion selector, AU inserts, meter.
- **Motion editor** — parameters for the selected motion; path drawing.
- **Cue list** — cues with Go, transition time, triggers; scene manager.
- **Settings** — output device/aggregate management, sample rate, buffer
  size, OSC/MIDI, clock source.

## 7. Project file

`.spacer` is JSON with a `schema_version`. Top level: `layout`,
`device_mapping`, `sources`, `scenes`, `cue_lists`, `osc`, `midi`, `clock`.
Audio files are referenced by path with a relative-to-project fallback;
"collect files" copies them next to the project. Plugin state is stored as
base64 blobs plus parameter values (for readability and for recovery if a
plugin is missing).

## 8. Reliability (live use)

- Output **limiter** on every speaker, always on, cannot be bypassed by a cue.
- Device disappears → outputs go silent, engine keeps running, UI warns; on
  return, resumes with the same mapping.
- Missing plugin → source plays dry, cue list continues, UI flags it.
- **Panic**: one key/OSC message fades all outputs to silence in 50 ms.
- Autosave of the project every few minutes and on every cue-list edit.
- **Installation mode**: launch at login, open last project, start cue list,
  no dialogs; optional watchdog that relaunches on crash.

## 9. Testing

- Spatializer unit tests against reference gains (VBAP triplet cases, DBAP
  known layouts, ambisonic encode/decode of known directions).
- Compensation tests: layouts with known geometry produce expected delays and
  gains.
- Motion tests: trajectories evaluated at known beat positions are
  deterministic (fixed seeds for random walks).
- Offline engine render: project + input WAVs → N-channel WAV compared to
  golden files.
- Cue transition tests: ramps are monotonic and complete in the stated time.
- RT-safety: allocation guard in debug; callback-time benchmark in CI with
  32 outputs, 32 sources, 5th-order ambisonics.

## 10. Milestones

| # | Milestone | Scope |
|---|-----------|-------|
| M0 | Skeleton | CMake + JUCE app, CoreAudio output, layout model, project file, stage view with static sources, VBAP, per-speaker compensation and limiter |
| M1 | Inputs | `Spacer.driver` virtual device, hardware inputs, file players with transport |
| M2 | Motion + clock | Internal clock, trajectories, step sequences, modulation; OSC and MIDI in |
| M3 | Scenes + cues | Scenes, cue lists, triggers, follow-on, loops, installation mode, autosave |
| M4 | Plugins + algorithms | AU hosting on sources/outputs; DBAP; ambisonics; distance model |
| M5 | External sync | Ableton Link, MIDI Clock/MTC, OSC clock |
| M6 | Polish | Layout presets library, OSC out, DMG + notarization, docs |

## 11. Open questions

1. Virtual device channel count: fixed 64, or user-configurable (requires
   driver reload)?
2. Should stereo sources be two linked points or a single point with width?
   (Proposal: both, as a per-source option.)
3. Coordinate convention: azimuth clockwise from front (SpatRevolution/IEM
   style) vs. counter-clockwise (mathematical). Proposal: clockwise from
   front, elevation up, distance in metres; expose a preference.
4. Should cue lists support nested/parallel cue lists (e.g. one for lights,
   one for motion)?
5. Licensing: JUCE is dual-licensed AGPLv3 (Spacer would have to be
   open-sourced under AGPL) or a flat commercial licence with no royalties:
   Starter free up to $20k/yr total revenue, Indie $800 perpetual up to
   $300k/yr, Pro $3,500 perpetual unlimited (figures as of late 2026; verify
   at juce.com). Plan: Starter now, Indie when revenue requires it. The
   no-licence alternative is a CoreAudio-direct engine with a Swift/AppKit UI,
   at the cost of rebuilding plugin hosting, OSC, MIDI and device management.
   Full option analysis and migration notes: `docs/JUCE-ALTERNATIVES.md`.
