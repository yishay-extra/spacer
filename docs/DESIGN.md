# Spacer — Design Document

> Status: **Draft / initial** · Owner: TBD · Last updated: 2026-11-02

Spacer is a multichannel audio router for advanced audio setups: it takes audio
from any number of input devices/streams, mixes and processes it through a
configurable routing graph, and delivers it to any number of output
devices/streams with low, predictable latency.

---

## 1. Goals

1. **Arbitrary N×M routing** — any input channel to any output channel(s), with
   per-route gain, mute, polarity and delay.
2. **Multiple devices at once** — aggregate several physical/virtual interfaces,
   including devices running on independent clocks.
3. **Low, predictable latency** — target ≤ 10 ms round trip at 48 kHz / 128
   frames on a typical desktop; never allocate, lock, or do I/O on the audio
   thread.
4. **Live reconfiguration** — routes, gains and nodes change at runtime without
   glitches (click-free ramps, no dropouts).
5. **Declarative configuration** — the whole setup is a human-readable file
   that can be versioned, diffed and reloaded.
6. **Headless-first** — runs as a daemon; UIs (CLI, web, desktop) are clients of
   a control API.
7. **Cross-platform** — Linux first, then macOS and Windows.

## 2. Non-goals (for v1)

- Being a DAW: no recording timeline, editing, or plugin hosting (VST/AU/LV2 may
  come later as a node type).
- Network audio transports (AES67/Dante/NDI) — designed for as a future
  backend, not implemented in v1.
- Sample-rate conversion quality on par with offline mastering tools; we target
  transparent real-time SRC, not audiophile batch SRC.

## 3. Core concepts

| Concept      | Description |
|--------------|-------------|
| **Device**   | An audio endpoint provided by a backend (ALSA, PipeWire/JACK, CoreAudio, WASAPI). Has a set of input and/or output channels, a sample rate and a clock. |
| **Port**     | A single mono channel on a device or node. Ports are typed `in` or `out`. |
| **Node**     | A processing unit in the graph: device source, device sink, mixer, gain, delay, EQ, meter, splitter, etc. |
| **Route**    | A connection from an output port to an input port, carrying gain/mute/polarity/delay parameters. |
| **Bus**      | A named group of ports (e.g. `stereo-main` = L/R) that can be routed as a unit. |
| **Graph**    | The full set of nodes and routes. Compiled into an immutable, topologically sorted *execution plan*. |
| **Clock domain** | Set of devices sharing one hardware clock. Each domain is driven by its own I/O callback. |
| **Scene**    | A named snapshot of route/parameter state that can be recalled atomically. |

## 4. Architecture

```
           ┌─────────────────────────── control plane (non-RT) ───────────────────────────┐
  clients  │                                                                              │
  ┌──────┐ │  ┌───────────┐   ┌──────────────┐   ┌────────────────┐   ┌───────────────┐   │
  │ CLI  │─┼─▶│ Control   │──▶│ Graph model  │──▶│ Graph compiler │──▶│ Plan swapper  │───┼──┐
  │ Web  │ │  │ API (RPC) │   │ (+ config)   │   │ (validate,sort)│   │ (lock-free)   │   │  │
  └──────┘ │  └───────────┘   └──────────────┘   └────────────────┘   └───────────────┘   │  │
           │        ▲                                                                     │  │
           │        │ meters / events (lock-free ring buffers)                            │  │
           └────────┼─────────────────────────────────────────────────────────────────────┘  │
                    │                                                                        │
           ┌────────┴──────────────────────── data plane (RT) ──────────────────────────┐    │
           │                                                                            │◀───┘
           │  Clock domain A callback          Clock domain B callback                  │
           │  ┌─────────┐  ┌────────────┐      ┌─────────┐  ┌────────────┐              │
           │  │ Dev A in│─▶│ Plan exec  │─────▶│ Bridge  │─▶│ Plan exec  │─▶ Dev B out  │
           │  └─────────┘  └────────────┘      │ (ASRC)  │  └────────────┘              │
           │                                   └─────────┘                              │
           └────────────────────────────────────────────────────────────────────────────┘
                    ▲                                   ▲
            ┌───────┴───────────────────────────────────┴───────┐
            │ Backend abstraction: ALSA · PipeWire/JACK ·        │
            │ CoreAudio · WASAPI · (future: AES67, file, null)   │
            └────────────────────────────────────────────────────┘
```

### 4.1 Control plane (non-real-time)

- **Control API** — local RPC (Unix socket / named pipe) with JSON messages;
  optional WebSocket for the web UI. Every mutation is a command
  (`add_route`, `set_gain`, `recall_scene`, …) applied to the graph model.
- **Graph model** — the authoritative, mutable description of the setup.
  Serializable to/from the config file.
- **Graph compiler** — validates (no cycles without explicit delay, channel
  counts match, devices exist), resolves buses to ports, assigns buffers,
  topologically sorts nodes per clock domain, and emits an immutable
  `ExecutionPlan`.
- **Plan swapper** — publishes the new plan to the RT thread via an atomic
  pointer swap. Old plans are reclaimed on the control thread (epoch / RCU
  style), never freed on the audio thread.

### 4.2 Data plane (real-time)

- One RT callback per clock domain, driven by the backend.
- Each callback: read device inputs → execute plan nodes in order → write
  device outputs → push meter data to a lock-free ring.
- Parameter changes that don't alter topology (gain, mute) go through a
  lock-free SPSC parameter queue and are applied with per-sample ramps
  (default 10 ms) to avoid zipper noise/clicks.
- Topology changes go through plan swap; the new plan crossfades from the old
  one over one or more buffers where routes are added/removed.

**RT rules** (enforced by review and, in debug builds, an allocation guard):
no heap allocation, no locks, no syscalls/logging, bounded execution time.

### 4.3 Clock domains and drift

Devices without a shared clock drift relative to each other. Spacer:

1. Groups devices into clock domains (auto-detected where the backend exposes
   it, overridable in config).
2. Picks one **master domain** (configurable; default: first output device).
3. Connects domains with a **bridge**: a lock-free ring buffer plus an adaptive
   resampler (ASRC) whose ratio is steered by a PI controller on buffer fill
   level.
4. Reports drift (ppm) and bridge latency via the control API.

### 4.4 Backends

A small trait/interface each platform implements:

```text
trait Backend {
    fn enumerate() -> Vec<DeviceInfo>;
    fn open(device, config: StreamConfig, callback: RtCallback) -> Stream;
    fn clock_domain_hint(device) -> Option<ClockId>;
}
```

Order of implementation: **PipeWire/JACK** (Linux, most valuable for advanced
setups) → ALSA (direct, low-level) → CoreAudio → WASAPI → `null`/`file`
backends for testing.

## 5. Built-in nodes (v1)

| Node        | Purpose |
|-------------|---------|
| `source`    | Device input channels into the graph |
| `sink`      | Graph into device output channels |
| `matrix`    | N×M mixer with per-crosspoint gain (the core router) |
| `gain`      | Gain / mute / polarity per channel |
| `delay`     | Per-channel delay (samples or ms) for alignment |
| `meter`     | Peak / RMS metering, published to control plane |
| `eq`        | Basic parametric biquad EQ (stretch goal for v1) |
| `tone`      | Test signal generator (sine, pink noise) |

Nodes implement a common interface: `prepare(sample_rate, max_frames)` (non-RT,
may allocate) and `process(inputs, outputs, frames)` (RT, must not allocate).

## 6. Configuration

TOML, loaded at startup and hot-reloadable (`spacer reload` or file watch).
Unknown keys are errors. Example:

```toml
[engine]
sample_rate = 48000
buffer_frames = 128
master_clock = "scarlett"

[devices.scarlett]
backend = "pipewire"
match = "Focusrite Scarlett 18i20"

[devices.hdmi]
backend = "pipewire"
match = "HDMI"

[buses]
mics      = { device = "scarlett", dir = "in",  channels = [1, 2, 3, 4] }
monitors  = { device = "scarlett", dir = "out", channels = [1, 2] }
tv        = { device = "hdmi",     dir = "out", channels = [1, 2] }

[[routes]]
from = "mics:1"
to   = ["monitors:1", "monitors:2"]
gain_db = -6.0

[[routes]]
from = "mics:2"
to   = "tv"          # bus-to-bus, mono to both channels
delay_ms = 12.5

[scenes.podcast]
"mics:1 -> monitors:*" = { gain_db = -3.0 }
"mics:2 -> tv"         = { mute = true }
```

## 7. Interfaces

- **CLI** — `spacer daemon`, `spacer devices`, `spacer routes`,
  `spacer route add mics:1 monitors:1 --gain -6`, `spacer scene recall podcast`,
  `spacer meter`.
- **Control API** — versioned JSON-RPC 2.0 over Unix socket; subscriptions for
  meters, device hot-plug and xrun events.
- **Web UI** (post-v1) — matrix view of routes, meters, scene buttons.

## 8. Reliability

- **Device hot-plug** — devices disappearing mark their nodes as *offline*
  (output silence/input zeros); routes are preserved and resume when the device
  returns.
- **Xruns** — counted and reported per domain; never crash the engine.
- **Safe defaults** — new routes start muted or at −∞ and ramp in; optional
  output limiter on sinks to protect speakers/ears.
- **Crash isolation** — control-plane errors never stall the RT thread.

## 9. Testing strategy

- **Unit tests** for nodes against reference signals (bit-exact where possible).
- **Graph compiler tests** — validation, cycle detection, bus expansion.
- **Offline engine tests** using the `file`/`null` backend: render a graph to
  WAV and compare against golden files.
- **Drift simulation** — two virtual clocks with configurable ppm offset to
  verify the ASRC controller converges and stays within latency bounds.
- **RT-safety checks** — allocation guard in debug builds; latency/jitter
  benchmarks in CI.

## 10. Technology choice (proposed)

**Rust** — memory safety without GC, good RT story (no hidden allocations if
disciplined), mature crates: `cpal` (cross-platform I/O, as a starting point),
`pipewire`/`jack` bindings, `rtrb` (lock-free ring buffers), `arc-swap`
(plan swapping), `rubato` (resampling), `serde`/`toml`, `tokio` for the control
plane. Alternative considered: C++ (JUCE) — richer audio ecosystem, but heavier
licensing and weaker safety guarantees.

## 11. Milestones

| #  | Milestone | Scope |
|----|-----------|-------|
| M0 | Skeleton  | Repo layout, CI, config parsing, `null` backend, graph model + compiler |
| M1 | Single-device router | One device (PipeWire/JACK), matrix + gain + meter nodes, CLI |
| M2 | Live control | Control API, parameter ramps, plan swap, scenes, hot reload |
| M3 | Multi-device | Clock domains, ASRC bridges, drift reporting, hot-plug |
| M4 | Platforms | CoreAudio + WASAPI backends |
| M5 | UI | Web UI with matrix view and meters |

## 12. Open questions

1. Should Spacer expose itself as virtual devices (e.g. a PipeWire node / macOS
   aggregate driver) so other apps can route *into* it, or only route between
   existing devices?
2. Floating-point internal format: `f32` everywhere, or `f64` for mixing sums?
3. Do we need sample-accurate scheduling of scene changes (e.g. MIDI/OSC
   triggers, timecode)?
4. Plugin hosting (LV2/VST3/CLAP) — in scope for v2?
5. Licensing: MIT/Apache-2.0 vs GPL (affects which backend libraries we can
   link)?
