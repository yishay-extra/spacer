# Spacer v1 — Implementation Plan

Work packages (WP) sized for one agent each. Every WP: read `CLAUDE.md`, the
spec sections listed, and the files named; implement; add tests; keep the
build warning-free; mark the WP done here with leftovers noted.

**Size** guides which agent tier to use: **S** (mechanical, ≤ ~300 lines,
cheap model), **M** (one subsystem, needs care), **L** (needs DSP or
platform expertise; use the strongest model).

**Platform**: WPs marked *core* build and test on Linux; the rest need macOS
(CI runs both). Anything touching `src/engine`, `src/control`, `src/ui`,
`driver/` is macOS.

## Dependency graph

```
WP0 ─┬─ WP1 (model+JSON) ─┬─ WP2a VBAP2D ─ WP2b VBAP3D ─ WP2d Ambisonics
     │                    ├─ WP2c DBAP
     │                    ├─ WP6 Clock ─ WP7 Motion ─┐
     │                    └─ WP3 Engine ─┬─ WP4 Sources ─ WP5 Driver
     │                                   ├─ WP8 OSC/MIDI
     │                                   ├─ WP9 Scenes/Cues  (needs WP7)
     │                                   ├─ WP10 Plugins
     │                                   └─ WP11a..e UI      (each after its subsystem)
     └─ WP13 Packaging (last)            WP12 External clocks (after WP6, WP8)
```

---

## WP0 — Scaffold ✅ done

CMake (core lib, JUCE app, Catch2 tests), CI, core interfaces
(`Position`, `Layout`, `Clock`, `Spatializer`, `Motion`), compensation,
Position tests, `CLAUDE.md`, spec, this plan.

Leftovers: JUCE pinned at 8.0.15; JUCE 9 exists and should be evaluated
(API and licence changes) before M3. App target not yet built in CI (first
macOS run will tell).

---

## WP1 — Core model and project file · **core · M**

Spec: §4, §8, §10 (specs only), §11 (types only), §15.

- Add to `src/core/`: `Source.h` (`SourceKind`, `SourceSpec`, `InputSpec`
  variants, `DistanceModel`), `MotionSpec.h` (tagged union of all motion
  kinds + `Period {beats|seconds}` + `ModulatorSpec`), `SpeakerChain.h`
  (`SpeakerChainSpec`, `Eq3`, `PluginSlot`), `Scene.h`, `Cue.h` (`Trigger`
  variants, `Cue`, `CueList`), `Project.h` (`Project`, `DeviceMapping`,
  `OscConfig`, `MidiConfig`, `ClockConfig`).
- `Json.h/.cpp`: nlohmann `to_json/from_json` for every type, following the
  §15 shapes exactly (`{"az","el","dist"}`, `{"beats":x}|{"seconds":x}`).
- `ProjectFile.h/.cpp`: `load(path) → expected<Project, Error>`,
  `save(project, path)`, `schemaVersion` check, migration hook (no-op for 1),
  unknown-key warning list.
- `Validate.h/.cpp`: layout rules (§7), source limits, unique ids, cue
  references resolve.
- `resources/layouts/*.json`: the §7 presets, loaded by a `LayoutPresets`
  helper (embedded via CMake `file(READ)` → generated header, or
  `configure_file`; no JUCE BinaryData in core).

Acceptance: round-trip test for every type; the §15 example parses; invalid
inputs produce errors not crashes; presets load and validate; tests pass on
Linux.

---

## WP2a — VBAP 2D · **core · M**

Spec: §6, §6.1 (2D), §6.5.

- `src/core/spatial/Vbap.h/.cpp` implementing `Spatializer` for layouts with
  all |el| < 5°: sort speakers by azimuth, pairwise gains, constant-power
  normalisation, MDAP spread, per-sample interpolation in `render`.
- `src/core/spatial/DistanceModel.h/.cpp`: gain + optional one-pole low-pass
  (§6.5). Keep DSP in core to this and the per-sample gain interpolation;
  all other DSP primitives (delays, EQ, limiter, smoothing) belong in
  `src/engine` built on `juce::dsp` — do not hand-roll them in core.

Acceptance: a source exactly at a speaker gives gain 1 there and 0 elsewhere;
midway between two adjacent speakers gives 1/√2 each; Σg²=1 everywhere (test
360 points); render of a constant signal matches `gainsFor`; no allocation
in `render` (test with a counting allocator hook).

---

## WP2b — VBAP 3D · **core · L**

Spec: §6.1 (3D).

- Convex-hull triangulation of speaker unit vectors (implement a small
  3D quickhull or incremental hull; no external dependency), imaginary floor
  / ceiling speakers when the hull does not enclose the origin, triangle
  search, `g = L⁻¹p`, MDAP spread with ring of virtual sources.
- Automatically chosen by `Vbap` when any |el| ≥ 5°.

Acceptance: 7.1.4 and dome-22 presets triangulate; source at any speaker →
unit gain there; source on an edge between two speakers → two non-zero
gains; Σg²=1 over a 1° grid of the sphere; a hemisphere layout works via
the imaginary floor; deterministic.

---

## WP2c — DBAP · **core · S**

Spec: §6.2. `src/core/spatial/Dbap.h/.cpp`. Acceptance: known-layout gains
match hand-computed values; Σg²=1; spread widens the gain distribution
(entropy increases monotonically with spread).

---

## WP2d — Ambisonics · **core · L**

Spec: §6.3.

- `AmbisonicEncoder` (orders 1–5, ACN/SN3D real SH, spread by order
  weighting) implementing `Spatializer::render` into `bus.ambisonic`.
- `AmbisonicDecoder` (AllRAD: t-design table for t ≥ 2N+1 embedded as a
  constant, VBAP-3D from t-design to layout, max-rE weights), `decode(bus)`
  adding to `bus.speakers`. Built once per layout/order change.

Acceptance: encoding a direction and decoding on a dense t-design layout
reproduces the direction (energy vector rE points within 2° for order 3);
encoder SH values for canonical directions match reference tables; decode
matrix shape and energy preservation tests; no allocation in render/decode.

---

## WP3 — Audio engine · **macOS · L**

Spec: §5 (all), §7 (effective delay/gain), §6.4 (Direct).

- `src/engine/AudioEngine.h/.cpp`: JUCE `AudioDeviceManager` + callback;
  `Graph` (pre-allocated sources, speaker bus, ambisonic bus, speaker chains);
  command FIFO (`Command` variants, §5.2), feedback FIFO (meters, positions,
  xruns), `SwapGraph` with state carry-over and one-block fade-out of removed
  sources.
- `SpeakerChain`: fractional delay line (max 100 ms), gain ramp, polarity,
  3-band EQ (biquads), insert slots (empty until WP10), look-ahead limiter
  (§5.5).
- `Direct` spatializer (trivial) in core.
- `OfflineRenderer` (§5.7) and a `GraphBuilder(Project) → Graph`.
- Allocation guard: in debug, hook `operator new` during the callback and
  assert.
- Aggregate device creation helper (CoreAudio `AudioHardwareCreateAggregateDevice`).

Acceptance: offline render of a 2-speaker project with a test tone source at
az −90 yields signal only on the left channel; compensation delay measured in
the render matches `computeCompensation`; limiter never exceeds −0.3 dBFS on
a +12 dB input; command FIFO survives 1M commands without allocation; CI
benchmark (§17) in place.

---

## WP4 — Sources · **macOS · M**

Spec: §8. Hardware inputs; `FilePlayer` with disk thread, ring buffer,
resampling, transport, loop; generators. Hook into `GraphBuilder`.
Acceptance: offline render with a file source matches the file (after
resample) within −80 dB; seek/loop are sample-accurate; stopping the disk
thread never blocks the audio thread (test with a stalled reader).

---

## WP5 — Virtual device driver · **macOS · L**

Spec: §14. `driver/` CMake target producing `Spacer.driver`; shared-memory
ring; app-side `VirtualInput` reader with fill-steered resampler; "Install
virtual device" action with privileged helper; version handshake.
Acceptance: `system_profiler SPAudioDataType` lists "Spacer" with 64
channels; a sender app's sine on channel 7 arrives on source channel 7 within
one driver buffer; driver absent → app runs with a warning. Manual test
checklist in `driver/TESTING.md`.

---

## WP6 — Internal clock ✅ done · **core · S**

Spec: §9. `InternalClock` (bpm, play/stop/continue, setBeat, tap, quantised
start). Acceptance: advancing 48000 samples at 120 bpm adds exactly 2 beats;
stop freezes beat; quantised start begins at the next bar boundary within
one block.

---

## WP7 — Motion · **core · L**

Spec: §10. All motion kinds in `src/core/motion/`, `MotionFactory(MotionSpec)`,
modulators (LFO, parameter paths, additive application), `ParameterPath`
resolver. Acceptance: for each kind, positions at known times match formulas;
beats-based periods follow bpm changes and freeze on stop; seconds-based
ones do not; `StepSequence` honours order/probability deterministically with
a seed; `reset()` restarts phase; no allocation in `evaluate`.

---

## WP8 — OSC and MIDI control · **macOS · M**

Spec: §12. `OscServer` (JUCE OSC, address router per §12.1, bundle timetags
scheduled to sample via command timestamps), `OscSender` (30 Hz feedback),
`MidiMapper` (learn, ranges, curves, note→cue, PC→scene), `ParameterPath`
→ `Command` translation. Acceptance: every §12.1 address has a test sending
a message and observing the resulting command; malformed messages are
counted, never crash; MIDI learn captures the first moved control.

---

## WP9 — Scenes, cues, cue lists · **core + macOS · M**

Spec: §11. Core: `SceneDiff(current, target) → ramp set`, cue list state
machine (Go/Back/Jump, loop, follow-on with random ranges), trigger
evaluation (beat, time of day). Control: `CueEngine` driving the command
queue; installation mode behaviour (autoload, arm, Go, autosave, no dialogs).
Acceptance: recall ramps complete in the stated time (offline render of a
gain change); discrete switches crossfade; cue list loops; `AfterPrevious`
with range stays within range over 1000 runs; autosave writes a valid file.

---

## WP10 — AudioUnit hosting · **macOS · L**

Spec: §13. Out-of-process scan with cache, insert slots on sources and
speakers, state in scenes, missing-plugin bypass, latency compensation,
parameter paths, editor windows. Acceptance: a known AU (e.g. Apple AUDelay)
loads in a source slot, its reported latency is compensated (offline render
alignment test), state round-trips through a scene, a project referencing a
missing AU still loads.

---

## WP11 — UI · **macOS**

Split so each lands after its subsystem:

- **WP11a Stage view + layout editor · L** (after WP3): §16.1, §16.4,
  presets, speaker test, compensation display, drag sources, side view.
- **WP11b Source panel · M** (after WP7): §16.2 incl. motion editors and
  modulators.
- **WP11c Cues panel + scene manager · M** (after WP9): §16.3.
- **WP11d Settings + status bar · S** (after WP5, WP8): §16.5, §16.6,
  driver install flow, aggregate creation.
- **WP11e Plugin UI · S** (after WP10): insert slots, editor windows.

Acceptance per sub-WP: manual checklist in `docs/ui-checklist.md`, no
UI call blocks the audio thread (allocation guard stays silent), undo/redo
for model edits.

---

## WP12 — External clocks · **macOS · L**

Spec: §9. `LinkClock` (Ableton Link SDK via FetchContent), `MidiClock` and
`MtcClock` (jitter-filtered PLL), `OscClock`. Switching through `SwapGraph`.
Acceptance: Link session with a reference app stays within 1 ms over 10
minutes (manual); MIDI clock from a test generator locks within 2 bars;
unit tests for the PLL with synthetic jitter.

---

## WP13 — Packaging · **macOS · M**

Signed + notarized `.app` and `.driver`, DMG, LaunchAgent for installation
mode relaunch, CI release job on tags, `docs/INSTALL.md`.

---

## Status log

| WP | Status | Notes |
|----|--------|-------|
| WP0 | done | see leftovers above |
| WP1 | done | Model, JSON, `projectfile::{parse,load,save,serialize}`, `validate()`, layout presets (embedded via `cmake/EmbedLayoutPresets.cmake`). Spec gaps filled and open questions: see "WP1 notes" below |
| WP2a | done | `spatial/Vbap` (2D, MDAP spread, LFE ignored) and `spatial/DistanceModel` (`distanceGain`, `distanceCutoffHz`, `DistanceProcessor`). Notes below |
| WP6 | done | `InternalClock` (tempo, play/resume/stop, setBeat, tap tempo, quantised start). Decisions and open points: see "WP6 notes" below |

### WP1 notes

Decisions made where SPEC was silent (all additive; nothing in the spec was changed):

- `SourceSpec` has no separate `kind` field; `kind()` is derived from the `InputSpec` variant so the two cannot disagree. JSON still has `"kind"` + `"input"`.
- Extra `SourceSpec` fields: `width`, `linkedPoints`, `motionRight` (second motion when `linkedPoints`), `directSpeakers` (targets for `Direct`, §6.4 says "selected in the source spec").
- `Project` gained `name`, `ambisonics {order}` (both in the §15 JSON but not in §4), plus `speakerChains` and `master` (a scene holds these, but the live state outside scenes had nowhere to live).
- `SpeakerChainSpec::polarity` is named `polarityInverted` (matches `Speaker`); added `mute` (OSC `/speaker/{id}/mute`).
- Stereo/mono input: JSON `{"channel":n}` vs `{"left","right"}`; `stereo` must agree (validated).
- File `relativeTo: "project"` is modelled as `FileInputSpec::relativeToProject` (path is relative to the project file).
- `LfoShape` = sine/triangle/saw/square/sampleAndHold (§10 lists none). `Cue::postWaitSeconds` (in §11, not §4/§15). `AfterPrevious` random range = `randomRange` extra in the same unit.
- `StepSequenceMotion` and `RandomWalkMotion` carry a `seed` / `glide` bool (needed for the §10 determinism rule).
- `AtBeat{bar, beat}` is 1-based in `validate()`.
- Unknown-key detection works by diffing the file against the re-serialised model.
- `load()` does not call `validate()`; callers decide (a project with a missing plugin or bad cue must still be openable for repair).

Spec issues for the owner (not changed):

1. **Resolved** (SPEC §7: `Speaker::lfe`, excluded from panning, Direct-only). §7 presets: 5.1 / 7.1 / 7.1.4 include an LFE speaker. It is in the same direction as C, which degenerates VBAP. Layouts probably need a way to mark non-panned speakers (e.g. `role: "lfe"`) — WP2a/WP2b must decide. `validate()` currently *warns* on coincident directions.
2. §7 "standard angles" is undefined for Dome 22 and the octagon; chosen: octagon at 0°,45°…; dome22 = 8 @0°, 8 @30° (offset 22.5°), 5 @60°, zenith.
3. §15 example has `"..."` placeholders inside `scenes`; not valid JSON as written.
4. §11 `Midi{note | cc}` and "program change selects scenes" (§12.2): program-change→scene mapping has no home in `MidiConfig`; not modelled.
5. §4 `Scene` says "full snapshot ... master" without fields; modelled as `MasterSpec {gainDb}`.

### WP2a notes

- `Vbap` skips `lfe` speakers (SPEC §7): gain 0 in `gainsFor` and silent in `render`; tested with the 5.1 preset (LFE shares C's direction, so C still gets gain 1).
- `Vbap::is2D()` reports whether all panned speakers are |el| < 5°. 3D layouts are WP2b; until then `Vbap` pans them by azimuth only.
- Coverage gaps ≥ 179.9° (behind stereo, etc.) cannot use the 2x2 solve; the bounding speakers are cross-faded constant-power by angle. Speakers coincident in azimuth: only the first is used.
- MDAP in 2D: the 8 ring sources are azimuth offsets `r·cos(2πk/8)`, `r = spread·90°` (projection of the ring onto the horizontal plane); amplitude gains are summed then normalised to Σg²=1.
- Per-sample gain ramp is `g0 + (g1−g0)·i/n` (end gain reached at the start of the next block).
- `DistanceProcessor` (gain ramp + one-pole LP, coefficient ramped) is the stateful part; the `DistanceModel` struct stays in `Source.h`.

Spec issues for the owner (not changed):

1. §6.1 2D mode does not say what happens in a ≥180° coverage gap (stereo, or a front-only array); the cross-fade above is my choice.
2. §6.1 MDAP ring in 2D is ambiguous (a ring of angular radius around a horizontal direction); see projection above.
3. §6.1 "sum gains and normalise" — amplitude sum was assumed (not power sum).
4. §6.5 with `q > 0` and distance ≤ 1 m the cutoff is 20 kHz, which still low-passes noticeably at 48 kHz with a one-pole; fine, but not transparent.

### WP6 notes

Decisions made where SPEC §9 was silent (`src/core/InternalClock.{h,cpp}`, `tests/InternalClockTests.cpp`):

- `continue` is a C++ keyword, so the transport method is `resume()`. `play()` honours `ClockConfig::quantisedStart`; `resume()` always starts immediately from the frozen beat. `play()` starts from the current beat, not from zero.
- `advance(n)` returns the state at the start of the block and moves the beat by the block's length, so the effect of an advance is observed in the next call (for example `advance(0)` after `advance(48000)` reads beat 2.0 at 120 bpm).
- Quantised start: the reported beat stays frozen while armed. The transport starts on the first sample where the grid beat reaches the next bar boundary, and any remainder of that block is added to the beat. A beat already on a boundary counts as "next", so the start is immediate. A tempo change while armed changes how fast the countdown runs.
- Tap tempo averages the last 4 intervals. A gap over 2 s starts a new series. Tempo is clamped to [20, 300] bpm. Taps are in engine seconds (the `ClockState::timeSeconds` timeline).
- `setBpm` and `setBeat` ignore non-finite input. `setBeat` on an armed clock re-targets the pending start.
- Not thread-safe. The engine must call these from the audio thread through the command queue.

Spec issue for the owner (not changed):

1. §9 / §4: `ClockState` has no field for the sample offset at which a quantised start falls inside a block. The start is sample-accurate in the core, but the engine cannot pass that offset on to motions or cue triggers. Either add a `startSampleOffset` (or the engine splits the block at the start) before WP9.

