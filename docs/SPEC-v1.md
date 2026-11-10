# Spacer v1 — Specification

> Status: **v1.0 draft** · This is the contract for implementation. Rationale
> lives in `DESIGN.md`; work breakdown lives in `PLAN.md`. Anything decided
> here is fixed for v1 unless this file is changed first.

## 1. Scope and fixed limits

Spacer v1 is a macOS (12+) desktop application that:

- receives audio from other apps via a virtual device, from hardware inputs,
  and from audio files;
- spatializes up to **64 sources** over **2–32 speakers** in any physical
  arrangement using VBAP, DBAP, ambisonics or direct assignment;
- compensates speaker distance (delay and gain) automatically;
- moves sources in real time, by programmed trajectories and step sequences,
  synced to a clock (internal in v1; Link / MIDI / OSC later);
- stores complete state in **scenes**, recalled by **cues** in **cue lists**,
  for live performance and unattended installations;
- is controlled by its UI, **OSC** and **MIDI**;
- hosts **AudioUnit** inserts per source and per speaker output.

| Limit | Value |
|-------|-------|
| Speakers per layout | 2 … 32 |
| Sources per project | ≤ 64 |
| Virtual device channels | 64 (fixed in v1) |
| Ambisonic order | 1 … 5 (ACN / SN3D) |
| Sample rates | follow output device; 44.1 / 48 / 88.2 / 96 kHz tested |
| Block sizes | 32 … 2048 |
| Internal sample format | float32 |
| Max plugin inserts per slot | 4 per source, 4 per speaker |
| Added latency target | ≤ 10 ms at 48 kHz / 128 (excluding virtual device hop and plugins) |

Out of scope for v1: binaural output, file export, Windows/Linux, network
audio, room measurement/FIR correction, recording.

## 2. Coordinate system

Fixed; implemented in `src/core/Position.h`.

- Origin = listening position = layout centre.
- **Cartesian**: `x` right, `y` front, `z` up, metres.
- **Spherical** (`Position`): `azimuth` degrees, 0 = front, **positive =
  clockwise** seen from above (+90 = right), range (−180, 180];
  `elevation` degrees, 0 = horizontal, +90 = up; `distance` metres ≥ 0.
- Conversion: `x = d·cos(el)·sin(az)`, `y = d·cos(el)·cos(az)`,
  `z = d·sin(el)`.
- Interpolation between positions is done in Cartesian space.
- UI shows azimuth in degrees by default; a preference may show
  counter-clockwise, but the model never changes.

## 3. Module structure and dependency rules

```
src/core      pure C++20 — Position, Layout, Project model + JSON, Spatializer
              implementations, Motion implementations, Clock interface +
              InternalClock, Scene/Cue model. No JUCE, no OS APIs, no generic
              DSP (that is engine's job, on juce::dsp). Unit-tested on Linux
              and macOS.
src/engine    JUCE — AudioEngine (device callback), SourceGraph, SpeakerChain,
              command/feedback queues, file players, OfflineRenderer.
src/control   JUCE — OscServer/Client, MidiMapper, CueEngine, ClockManager
              (Link/MIDI/OSC clocks), PluginHost.
src/ui        JUCE — components.
src/app       entry point, MainWindow, app-level wiring.
driver/       Spacer.driver (HAL plugin), standalone.
```

`core` depends on nothing in-repo and never includes JUCE. Everything else
depends on `core`. `driver` depends on nothing.

## 4. Core interfaces

Defined in `src/core/*.h`; summarised here. Implementations must match these
signatures.

```cpp
struct Position { float azimuth, elevation, distance; /* + conversions */ };
struct Speaker  { int id; std::string name; Position position; float trimDb;
                  float userDelayMs; bool polarityInverted; int outputChannel; bool lfe; };
struct Layout   { std::string name; std::vector<Speaker> speakers; };
std::vector<SpeakerCompensation> computeCompensation(const Layout&, float c = 343.f);

struct ClockState { double bpm, beat; bool playing; double sampleRate, timeSeconds; int beatsPerBar;
                    int startOffset; double startBeat; /* §9 */ };
class ClockSource { ClockState advance(int numSamples); ClockState current() const; };

struct SpeakerBus { float* const* speakers; int numSpeakers;
                    float* const* ambisonic; int ambisonicOrder; int numSamples; };
class Spatializer { void prepare(const Layout&, double sr, int maxBlock);
                    void render(const float* mono, int n, const Position& start,
                                const Position& end, float spread, SpeakerBus&);
                    void gainsFor(const Position&, float spread, std::span<float>) const; };

struct MotionContext { ClockState clock; int numSamples; double sampleRate; };
class Motion { void reset(); Position evaluate(const MotionContext&); Position current() const; };
```

Additional core types to be added (WP1, WP9), with JSON (de)serialisation:

```cpp
enum class SourceKind { VirtualInput, HardwareInput, File, Generator };
enum class SpatializerKind { Vbap, Dbap, Ambisonic, Direct };
struct SourceSpec { int id; std::string name; SourceKind kind; /* kind-specific input spec */
                    bool stereo; float gainDb; bool mute; SpatializerKind spatializer;
                    DistanceModel distance; MotionSpec motion; float spread;
                    std::vector<PluginSlot> inserts; };
struct SpeakerChainSpec { int speakerId; float trimDb; float userDelayMs; bool polarity;
                          Eq3 eq; std::vector<PluginSlot> inserts; };
struct Scene { std::string name; /* full snapshot: sources, speaker chains, master {gainDb} */ };
struct Cue   { std::string name; std::string sceneName; float transitionSeconds;
               Trigger trigger; bool restartMotionPhase; };
struct CueList { std::string name; std::vector<Cue> cues; bool loop; };
struct Project { int schemaVersion; Layout layout; DeviceMapping devices;
                 std::vector<SourceSpec> sources; std::vector<Scene> scenes;
                 std::vector<CueList> cueLists; OscConfig osc; MidiConfig midi; ClockConfig clock; };
```

## 5. Engine

### 5.1 Threads

| Thread | Does | Must not |
|--------|------|----------|
| **Audio** (CoreAudio callback) | clock advance → motions → sources (input, inserts, gain, spatializer) → speaker bus → ambisonic decode → speaker chains → device output; publishes meters/positions | allocate, lock, block, log, touch UI |
| **Disk** (file players) | fills per-file ring buffers ahead of playback | — |
| **Message** (JUCE main) | UI, project model, OSC/MIDI parsing, cue scheduling, plugin editors | long work (>10 ms) |
| **Scan** (out of process) | AudioUnit scanning | — |

### 5.2 Communication

- **Control → audio**: a lock-free SPSC FIFO of `Command` (fixed-size POD
  variants: `SetGain{sourceId, dB}`, `SetPosition{sourceId, Position, smoothMs}`,
  `SetMotionParam{...}`, `SetModulator{sourceId, index, …}` (live modulator
  edit, LFO phase kept), `RecallScene{plan*}` (a recall plan built on the
  message thread from the live state and the target scene),
  `SwapGraph{newGraphPtr}` …). Capacity 4096; overflow is dropped and counted
  on the producer side (the count is reported to the UI as `CommandDropped`).
  Timed commands (`pushAt(cmd, engineSample)`, e.g. from OSC timetags) are
  applied at that sample by splitting the block; up to 1024 may be pending.
  **Single producer:** only the message thread pushes commands. OSC, MIDI and
  other control callbacks that run on their own threads hand events to the
  message thread first (or through their own SPSC queue drained there); they
  never push to the audio queue directly.
- **Audio → control**: a lock-free FIFO/ring for `Feedback` (`Meter{...}`,
  `Position{...}`, `ClockState`, `Xrun`, `CommandDropped`), polled at 30 Hz
  by the message thread.
- **Structural changes** (add/remove source, layout change, plugin insert
  change, sample-rate change): the message thread builds a complete new
  `Graph` (all buffers pre-allocated), sends `SwapGraph`; the audio thread
  swaps the pointer and returns the old one via feedback for deletion. Sources
  that exist in both graphs keep their state (ramps, motion phase, plugin
  instances). Removed sources are faded out over one block in the old graph
  before the swap is applied.

### 5.3 Signal path (per block)

```
for each source:
    in  = read input (virtual/hardware/file/generator), mono or stereo
    in  = AU inserts (pre-spatializer)
    in *= gain ramp (mute = −∞ dB ramp)
    pos = motion.evaluate(ctx)  (end-of-block), interpolated from previous end
    for each channel of the source (1 or 2, each with its own position):
        spatializer.render(in, n, posStart, posEnd, spread, bus)

ambisonic bus (if any ambisonic source): decode → add to speaker bus

for each speaker i:
    s  = bus.speakers[i]
    s  = delay(compensation + user)            // integer + fractional samples
    s *= gain(compensation + trim)             // ramped
    s  = polarity, 3-band EQ
    s  = AU inserts
    s  = limiter                               // always on
    device.out[channel(i)] = s
master: mute/panic ramp applied to all outputs
```

### 5.4 Ramps and smoothing

- All gains are ramped linearly in the linear domain over `smoothMs`
  (default **10 ms**; cue transitions use the cue's time).
- Positions: motion output is evaluated once per block; the spatializer
  interpolates gains per sample from the block's start to end position. Manual
  positions additionally pass through a one-pole smoother whose time constant
  is the Manual motion's `smoothMs` (default 20 ms); this is the only Manual
  smoothing (no additional glide).
- Delay changes (layout edits) crossfade between old and new delay over 50 ms.

### 5.5 Output limiter (per speaker)

Look-ahead peak limiter: ceiling −0.3 dBFS, look-ahead 1.5 ms, release 80 ms,
auto-makeup off. Cannot be bypassed by cues; UI shows gain reduction. Adds
1.5 ms latency, included in the latency budget.

### 5.6 Latency reporting

Engine reports: device buffer, limiter look-ahead, max plugin latency per
chain, virtual-device hop (if active). Plugin latency is compensated by
delaying other sources/speakers so that chains align.

### 5.7 Offline renderer

`OfflineRenderer(project, inputs: map<sourceId, wav>, seconds) → N-channel wav`
runs the same `Graph` without a device. Used for golden tests and for
"bounce layout test" in the UI later.

## 6. Spatializers

All operate on a mono signal per source channel; stereo sources are two
channels with positions `centre ± width/2` in azimuth (see §8). `spread` ∈
[0,1]. Gains are computed at block start and end and linearly interpolated per
sample. Energy normalisation: for VBAP/DBAP, gains are normalised so that
Σ g² = 1 (constant power).

### 6.1 VBAP

- **2D mode** when all speakers have |elevation| < 5°: pairwise panning on
  the ring sorted by azimuth. Source elevation is ignored. Speakers that
  coincide in azimuth: only the first (lowest index) is used. A coverage gap
  ≥ 180° between adjacent speakers (stereo, front-only arrays) cannot use the
  2×2 solve: inside it the two bounding speakers are cross-faded with a
  constant-power law by angle across the gap.
- **3D mode** otherwise: triangulate the speaker directions (convex hull of
  unit vectors; if the hull does not enclose the origin, add a virtual
  speaker at −Z — the "imaginary floor" — whose gain is discarded). Find the
  triangle containing the source direction, solve `g = L⁻¹·p` (Pulkki 1997),
  clamp negatives to 0, normalise.
  - Imaginary speakers: try, in order, none → −Z floor → +Z ceiling → both;
    use the first hull that encloses the origin, where "encloses" means every
    face plane is at least sin 5° from the origin. Imaginary gains are
    discarded and the rest renormalised; a source exactly at an imaginary
    speaker is shared equally (1/√k) over its k real hull neighbours.
  - Coplanar faces (> 3 points) are fanned from the lowest-index vertex.
  - Layouts that cannot span a volume even with ±Z fall back to 2D mode.
- **Spread**: implement MDAP: render the source as 1 + 8 virtual sources on a
  ring of angular radius `r = spread·90°` around the direction; sum the
  virtual sources' **amplitude** gains per speaker, then normalise (Σ g² = 1).
  In 2D mode the ring is projected onto the horizontal plane: the virtual
  sources are at azimuth offsets `r·cos(2πk/8)`, k = 0…7. In 3D mode they lie
  on the sphere at angular distance `r` from the direction, evenly spaced in
  bearing.
- Sources inside the array (distance < nearest speaker distance) use the
  direction only; the distance model (§6.5) handles level.

### 6.2 DBAP

Lossius et al.: `g_i = 1 / d_i^α` with `α = a / (20·log10 2)` (≈ a / 6.02)
and `d_i = |p − s_i| + r_s` (Cartesian, metres), rolloff `a` in dB per
doubling of distance (default 6, i.e. α ≈ 1),
spatial blur `r_s` (default 0.2 m), normalised so Σ g² = 1. Works with any
layout, no hull required. `spread` maps to `r_s = 0.2 + spread·3.0` m.

### 6.3 Ambisonics

- Encoder per source: order `N` (project setting, 1–5), ACN channel order,
  SN3D normalisation, real spherical harmonics, direction only. Spread via
  order weighting (lower effective order as spread → 1).
- One decoder per project, rebuilt on layout change: **AllRAD** (Zotter &
  Frank): t-design sampling of the sphere (t = 2N+1 or higher), VBAP from the
  t-design points to the actual layout (3D mode, with imaginary speakers at
  ±Z when needed), decoder = VBAP gains · sampling decoder. Apply max-rE
  weighting by default.
- Decode matrix is precomputed; per block the decode is a matrix multiply.
- Normalisation: the matrix is scaled so the mean decoded energy of a unit
  plane wave is 1 (matching Σ g² = 1 for VBAP/DBAP) — averaged over the
  sphere for 3D layouts and over the horizontal plane for horizontal ones
  (every non-LFE speaker below 5° elevation). `ambisonics.maxRe` (default
  true) selects max-rE weighting. The t-design → layout
  VBAP step always runs in 3D mode, even for horizontal layouts.
- Spread: effective order `N_eff = N·(1 − spread)`; degrees ≤ ⌊N_eff⌋ get
  weight 1, the next degree its fractional part, higher degrees 0 (spread 1 =
  omnidirectional). Weights are energy-compensated against the project's
  actual decoder (per-degree energy), so loudness is constant over spread.

### 6.4 Direct

Source goes to the speaker(s) selected in the source spec with unit gain
each; `spread` and position are ignored for rendering (position still shown in
UI).

### 6.5 Distance model (all panning spatializers)

Applied as a gain (and optional filter) to the source signal before
rendering, for every spatializer except `Direct` (which ignores position): `g = (r_ref / max(d, r_min))^k`, with `r_ref` = 1 m, `r_min`
default 0.5 m, `k` default 1.0 (0 disables). Optional one-pole low-pass whose
cutoff falls with distance: `fc = 20 kHz · (r_ref / max(d, r_ref))^q`, `q`
default 0 (off). Parameters are per source.

## 7. Speaker layout and compensation

- `computeCompensation`: `delay_i = (d_max − d_i) / c`, `gain_i = 20·log10(d_i / d_max)`
  (so the farthest speaker is 0 dB / 0 ms, nearer ones are delayed and
  attenuated). `c` = 343 m/s, user-adjustable in settings.
- Effective speaker delay = compensation + `userDelayMs`; effective gain =
  compensation + `trimDb`. `Speaker` (layout) holds the calibration defaults;
  if a `SpeakerChainSpec` exists for that speaker (live state or recalled
  scene), its `trimDb`, `userDelayMs` and polarity override the layout's.
  Mute and EQ exist only on the chain.
- **LFE speakers** (`lfe: true`, JSON key optional, default false) are
  excluded from panning: VBAP, DBAP and the Ambisonic decoder ignore them
  (they are not part of the hull, the DBAP sum or the AllRAD target layout).
  They receive signal only via `Direct` routing (§6.4). They still get
  distance compensation and a speaker chain. Bass management is out of
  scope for v1. Direction-coincidence checks skip LFE speakers.
- Validation: 2 ≤ speakers ≤ 32, unique ids, unique output channels (or −1),
  distance > 0.
- Layout presets (JSON files in `resources/layouts/`): stereo, quad, 5.1,
  7.1, 7.1.4, octagon, 12-ring, 16-ring, dome 22. All at 2 m, standard
  angles (ITU for 5.1/7.1/7.1.4). Rings start at 0° and are evenly spaced;
  dome 22 = 8 @ el 0°, 8 @ el 30° (offset 22.5°), 5 @ el 60°, 1 zenith.

## 8. Sources

| Kind | Input spec | Notes |
|------|------------|-------|
| `VirtualInput` | `channel` (0–63) or `{left, right}` | from Spacer.driver via shared memory |
| `HardwareInput` | device channel index(es) | only channels of the engine's device/aggregate |
| `File` | path, `loop`, `gainDb`, `startSeconds` | disk-streamed; transport: play/stop/seek; `playOnSceneRecall` |
| `Generator` | `sine{hz}`, `pink`, `white`, `impulse{periodSeconds}` | for layout testing |

- Stereo sources: `width` degrees (default 60); left at `az − width/2`, right
  at `az + width/2`, same elevation and distance. A per-source option
  `linkedPoints` instead gives each channel its own position/motion.
- File formats: WAV, AIFF, FLAC, OGG (JUCE), MP3/AAC/ALAC (CoreAudio). Files
  are resampled to the engine rate on the disk thread.
- File transport: a file source starts **stopped** at `startSeconds`, already
  buffered. `loop` repeats `[startSeconds, end)`; `play` at the end of a
  non-looping file restarts from `startSeconds`. The offline renderer plays
  files from t = 0.
- Generators are mono at full scale (level via `gainDb`): sine peak 1 from
  phase 0; white uniform ±1; pink (Voss–McCartney) peak ≤ 1; impulse one
  sample of 1.0 per period; fixed seed (deterministic renders).
- Live edits of a motion **period** are structural (rebuilt via `SwapGraph`)
  and re-anchor the phase so the position is continuous at the switch.

## 9. Clock

`ClockSource::advance(n)` is called first in every block. `ClockState.beat`
advances by `n / sampleRate / secondsPerBeat` while `playing`.

- `advance` returns the state at the **start** of the block.
  `ClockState.startOffset` is the sample index within that block at which
  the transport starts (a quantised start, or `play`/`continue`), or −1 if
  it does not start in this block; `startBeat` is the beat at that sample
  (valid when `startOffset >= 0`; a quantised start jumps from the frozen
  beat to the bar boundary). The cue engine uses them for
  sample-accurate beat triggers; motions may ignore it (at most one block
  of lag at a start, then exact because phase is derived from `beat`).

- **InternalClock** (v1): `bpm` (20–300), `play/stop/continue`, `setBeat`,
  `tap`, quantised start option (next bar). Transport commands arrive via
  the command queue and take effect at the next block.
- Later: `LinkClock`, `MidiClock`, `MtcClock`, `OscClock` implement the same
  interface; only one is active; switching happens through `SwapGraph`.
- Everything that references time uses **beats** (musical) or **seconds**
  (absolute); each motion/cue parameter says which.
- External clocks are latency-compensated: the reported beat is the
  reference beat at the time the block is *heard* (device output latency +
  engine latency), plus a per-project `clock.latencyTrimMs` (±1000 ms).

## 10. Motion

Phase origin: a new motion, and every reset (e.g. cue `restartMotionPhase`),
puts phase 0 at the start of the next evaluated block; phase is then derived
from the clock (`beat` or `timeSeconds`) relative to that origin, so it never
drifts. Beat-grid alignment is obtained by resetting on a bar boundary.

Each source (or each channel when `linkedPoints`) has one `MotionSpec`:

| Kind | Parameters | Position at time |
|------|------------|------------------|
| `Manual` | `position`, `smoothMs` | last set value |
| `Orbit` | `centre` (Position), `radius` m, `period` (beats or seconds), `direction` ±1, `phaseOffset` deg, `elevationWobble` deg, `wobblePeriod` | centre + radius·(sin θ, cos θ, 0) rotated to centre frame (the orbit *plane* is tilted: pitch by centre elevation, then yaw by centre azimuth), θ = 2π·(t/period)·dir + phase |
| `Line` | `from`, `to`, `period`, `mode` (`once`, `loop`, `pingpong`), `easing` (`linear`, `smooth`) | lerp(from, to, f(t)) |
| `Lissajous` | `centre`, `ax, ay, az` m, `fx, fy, fz` cycles per period, `period`, `phases` | centre + (ax·sin(2π fx t+φx), ay·sin(2π fy t+φy), az·sin(2π fz t+φz)) |
| `Path` | `points` (Cartesian list), `period`, `mode`, `interpolation` (`linear`, `catmullrom`), `constantSpeed` | parametric along polyline/spline |
| `RandomWalk` | `bounds` (box or sphere), `stepMetres`, `stepPeriod`, `seed`, `glide` | new target every stepPeriod, glide between |
| `StepSequence` | `steps[]`: `{target: Position \| speakerId, duration (beats/s), transition: jump\|glide, glideTime, probability}`, `order` (`forward`, `pingpong`, `random`), `loop` | step index advanced on duration; glide = lerp over glideTime |

- Periods given in **beats** derive phase from `clock.beat` (so tempo
  changes keep sync and a stopped transport freezes motion); periods in
  **seconds** derive phase from `clock.timeSeconds` (free-running).
- `reset()` sets phase to 0 / step 0; cue recall with `restartMotionPhase`
  calls it.
- **Modulators**: up to 4 per source: `{source: lfo{shape, period} | osc{address} | midi{cc}, target: parameter path, depth, offset}`. Applied additively before the motion is evaluated. Parameter paths are strings like `motion.radius`, `gain`, `spread`, `distance.k`.
- Determinism: with a fixed seed and a fixed sequence of `MotionContext`,
  every motion produces identical output (tested).

## 11. Scenes, cues, cue lists

- **Scene** = full snapshot of all mutable state (every source spec incl.
  motion + modulators + inserts' parameter state, every speaker chain,
  master). Layout and device mapping are *not* in scenes.
- **Recall(scene, transitionSeconds, restartMotionPhase)**: for every
  parameter, ramp from current to target over the transition. Discrete values
  (spatializer kind, motion kind, insert add/remove) switch at the start of
  the transition with a 50 ms crossfade. Sources absent from the scene fade
  out and are removed; new ones fade in.
- **Cue** = `{name, sceneName, transitionSeconds, trigger, restartMotionPhase, postWait}`.
  Triggers: `Manual` (Go), `AtBeat{bar, beat}` (when clock passes it),
  `AfterPrevious{seconds | beats}`, `Osc{address}`, `Midi{note | cc}`,
  `TimeOfDay{hh:mm, days}`.
- **Cue list**: ordered cues, `loop`, current index, Go / Back / Jump. Only
  one cue list is *armed* at a time. "Follow-on" = `AfterPrevious` triggers,
  which can carry a random range for installation variation.
- **Installation mode** (app preference): on launch open last project, arm the
  default cue list, Go; suppress dialogs; autosave; relaunch on crash via a
  LaunchAgent (WP13).
- Editing a scene while live: changes are applied immediately and marked
  "modified"; **Update scene** writes back (capturing live plugin state
  first, §13); **Revert** recalls.

## 12. Control: OSC and MIDI

### 12.1 OSC (in: UDP, default port 9000; out: configurable host/port)

Address space (`{id}` = integer source id or quoted name):

```
/spacer/source/{id}/pos/aed      f f f   azimuth elevation distance
/spacer/source/{id}/pos/xyz      f f f
/spacer/source/{id}/az           f
/spacer/source/{id}/el           f
/spacer/source/{id}/dist         f
/spacer/source/{id}/gain         f       dB
/spacer/source/{id}/mute         i       0/1
/spacer/source/{id}/spread       f       0..1
/spacer/source/{id}/motion/{param} f     e.g. /motion/radius, /motion/period
/spacer/source/{id}/file/play    (none) | /stop | /seek f
/spacer/speaker/{id}/trim        f
/spacer/speaker/{id}/mute        i
/spacer/{source|speaker}/{id}/insert/{slot}/param/{index}  f  (0..1)
/spacer/master/gain              f
/spacer/panic                    (none)
/spacer/cue/go                   (none)
/spacer/cue/back                 (none)
/spacer/cue/jump                 i | s
/spacer/scene/recall             s [f]   name, transition seconds
/spacer/clock/bpm                f
/spacer/clock/play | /stop | /continue
/spacer/clock/beat               f       (for OscClock, WP12)
```

Out (optional, 30 Hz): `/spacer/source/{id}/pos/aed`, `/spacer/meter/speaker/{id} f`,
`/spacer/clock/beat f`, `/spacer/cue/current i`.

Bundles with timetags are respected: scheduled to the sample whose audio
callback runs at the timetag (callback time, not heard time). Unknown
addresses are ignored and counted.

### 12.2 MIDI

- MIDI Learn on any parameter path (same paths as modulators). 7-bit and
  14-bit CC, NRPN, pitch bend; notes trigger cues. Program change: when
  `midi.programChangeChannel` is 1–16 (0 = off, default), program N on that
  channel recalls scene index N (0-based) with a 0 s transition. Mappings
  and `programChangeChannel` are stored in the project.
- Per-mapping range (min/max) and curve (linear/log).

## 13. Plugin hosting

- AudioUnit v2 (`JUCE_PLUGINHOST_AU=1`); AUv3 later. Scanning out of process;
  results cached in the app's support dir.
- Insert slots: ≤ 4 per source (pre-spatializer, mono or stereo matching the
  source) and ≤ 4 per speaker (mono, post-compensation, pre-limiter).
- State stored in scenes as `{pluginId, stateBase64, params: {index: value}}`.
  Missing plugin → slot bypassed, warning shown, project still loads.
- Reported plugin latency is compensated (§5.6).
- Plugin parameter paths: `source.{id}.insert.{slot}.param.{index}` and
  `speaker.{id}.insert.{slot}.param.{index}` (normalised 0..1) — usable by
  OSC, MIDI and (WP14) modulators. Slots may be `bypassed` (20 ms fade,
  latency kept).
- Scene capture (**Update scene**, save, before a recall) first captures live
  plugin state into the model (`PluginHost::captureState`).
- Full scans run off the message thread; each plugin is still scanned in its
  own process.

## 14. Virtual device (`Spacer.driver`)

- CoreAudio `AudioServerPlugIn`, bundle id `org.spacer.driver`, one device
  "Spacer" with **64 output channels** (as seen by sender apps), 44.1–96 kHz,
  float32, following the sample rate the Spacer app sets.
- Transport: a POSIX shared-memory ring (interleaved 64-channel frames,
  ≥ 8192 frames; created by the driver, opened by the app) + a sequence counter; the app reads it in its callback.
  Rate mismatch between driver and app is handled by the app resampling
  (slow drift expected; use a simple fill-level-steered resampler).
- The driver has no UI. Installed by the app ("Install virtual device…")
  with admin prompt; the app detects and reports driver version.
- Added latency: target buffering of max(selected, sender IO buffer) frames
  (selected default 512 at 48 kHz ≈ 10.7 ms; user-selectable 128/256/512,
  an app setting, not stored in the project).
- Open risk (verify on a Mac, docs/mac-validation/WP5.md): coreaudiod's
  sandbox may refuse `shm_open`; if so the transport moves to a mach shared
  memory handle (e.g. via XPC) with the same ring layout.

## 15. Project file

`*.spacer` = JSON, UTF-8, `schemaVersion: 1`. Loading an older version runs
migrations; loading a newer one fails with a clear message. Unknown keys are
ignored on load (forward compatibility) but a warning is logged.

```json
{
  "schemaVersion": 1,
  "name": "Dome show",
  "layout": {
    "name": "Dome 22",
    "speakers": [
      {"id": 1, "name": "1", "position": {"az": 0, "el": 0, "dist": 4.2},
       "trimDb": 0, "userDelayMs": 0, "polarityInverted": false, "outputChannel": 0,
       "lfe": false}
    ]
  },
  "devices": {"outputDeviceName": "Spacer Aggregate", "sampleRate": 48000, "bufferSize": 128,
              "speedOfSound": 343.0},
  "ambisonics": {"order": 3},
  "sources": [
    {"id": 1, "name": "Synth", "kind": "VirtualInput", "input": {"left": 0, "right": 1},
     "stereo": true, "width": 60, "gainDb": -6, "mute": false,
     "spatializer": "Vbap", "spread": 0.1,
     "distance": {"k": 1.0, "rMin": 0.5, "q": 0},
     "motion": {"kind": "Orbit", "centre": {"az": 0, "el": 20, "dist": 0},
                "radius": 3.0, "period": {"beats": 16}, "direction": 1, "phaseOffset": 0},
     "modulators": [],
     "inserts": []}
  ],
  "scenes": [{"name": "Intro", "sources": [], "speakerChains": [],
              "master": {"gainDb": 0}}],
  "cueLists": [{"name": "Show", "loop": false,
                "cues": [{"name": "Start", "scene": "Intro", "transitionSeconds": 2.0,
                          "trigger": {"kind": "Manual"}, "restartMotionPhase": true}]}],
  "osc": {"inPort": 9000, "out": {"host": "127.0.0.1", "port": 9001, "enabled": false}},
  "midi": {"mappings": [], "programChangeChannel": 0},
  "clock": {"kind": "Internal", "bpm": 120, "beatsPerBar": 4}
}
```

Positions serialise as `{"az","el","dist"}`. Periods as `{"beats": x}` or
`{"seconds": x}`. File paths as given plus `relativeTo: "project"` fallback.

## 16. UI (v1)

Single window, dark theme, resizable, minimum 1100×700. Panels:

1. **Stage** (centre): 3D view of layout and sources (orthographic, the floor
   always level). Drag empty space to rotate, wheel to zoom; snap buttons
   3D / Top / Side / Front. A stem to the floor shows the height of every
   speaker and source. Drag sources (sets Manual motion value or offsets the
   motion centre); a drag moves in the screen plane. Trajectories drawn as
   faint paths. Each source has its own colour and is drawn as a cloud whose
   density follows its level, with a line to, and a smaller cloud on, each
   speaker that carries it (VBAP, DBAP, Direct; ambisonic sources show the
   cloud only). Speaker meters as rings; click a speaker to solo pink noise.
   Keyboard: space = Go, Esc = panic, ⌘Z undo.
2. **Sources** (left): list + strip for the selected source (input, gain,
   mute, spatializer, spread, distance, motion kind + params, modulators,
   inserts, meter). Add/remove/duplicate.
3. **Cues** (right): armed cue list, Go/Back, current/next highlighted,
   transition times, triggers; scene manager (recall/update/new).
4. **Layout editor** (modal or tab): speaker table + stage edit; presets;
   computed compensation shown read-only; output channel assignment; test
   each speaker.
5. **Settings** (modal): audio device / create aggregate, sample rate, buffer,
   driver install, OSC ports, MIDI devices, clock source, installation mode.
6. **Status bar**: CPU %, xruns, latency, clock (bpm, bar.beat), driver status.

## 17. Testing and quality

- Core: Catch2 unit tests for every spatializer (known-gain cases), compensation,
  every motion (determinism + known values), scene recall ramps, JSON round
  trips, migrations.
- Engine: offline-render tests against analytic references (preferred) or
  golden files (`tests/golden/*.wav`), tolerance −80 dBFS RMS difference;
  latency reporting tests.
- RT safety: debug-build allocation guard (`operator new` hook active during
  the callback asserts), plus a CI benchmark: 64 sources, 32 speakers,
  5th-order ambisonics + VBAP mix, 128 frames at 48 kHz must process in
  < 1.0 ms on the CI runner.
- Warnings are errors in CI. `clang-format` enforced.

## 18. Definition of done (any work package)

1. Code in the right module, respecting the dependency rules.
2. Tests added and passing on Linux (core) and macOS (all).
3. No new warnings; no allocations/locks in RT paths.
4. Spec sections touched are still true (or the PR says what must change).
5. Short note in `PLAN.md` marking the WP done with anything left over.
