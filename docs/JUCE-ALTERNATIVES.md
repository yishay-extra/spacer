# Replacing JUCE — options and cost (reference, not planned)

Status: **not adopted.** v1 stays on JUCE (see `DESIGN.md` §3 and §11.5).
This note records what a JUCE-free stack would look like so the decision can
be revisited later without redoing the analysis. Written 2026-11-02, after
WP1/WP6 landed and before WP3 (the first WP that uses JUCE).

## When it is worth doing

JUCE is already open source under AGPLv3. If Spacer is published under AGPL,
JUCE costs nothing. Replacing it only pays off if Spacer must be
closed-source **and** the commercial licence fee is unwanted, or if a
specific JUCE limitation shows up (e.g. a UI the JUCE component model makes
hard).

## What is already portable

- `src/core` has no JUCE dependency (enforced by `CLAUDE.md`): model, JSON,
  project file, validation, layouts, spatializer/motion/clock maths. It is
  reused unchanged by any stack.
- The engine ↔ UI boundary is the model plus lock-free queues (SPEC §5.2),
  so the UI toolkit can change independently of the engine.

## Replacement per JUCE component

| JUCE part (where used) | Free replacement | Licence | Effort |
|---|---|---|---|
| `AudioDeviceManager`, CoreAudio I/O (WP3) | CoreAudio HAL / AUHAL directly; or miniaudio, RtAudio | Apple SDK; MIT-0 / MIT | Low–medium |
| `juce::dsp` delay line, IIR/biquad EQ, `SmoothedValue`, limiter (WP3) | In-house: fractional delay line, RBJ biquads, linear/one-pole smoothing, look-ahead peak limiter | ours | Low, but needs strict reference tests |
| Resampling / `Oversampling` (WP4, limiter) | libsamplerate, r8brain-free-src | BSD / MIT | Low |
| Audio file decoding (WP4) | ExtAudioFile / AVAudioFile (macOS); or dr_libs, miniaudio | Apple SDK; public domain / MIT | Low |
| OSC (WP8) | oscpack, tinyosc; liblo | permissive; LGPL | Low |
| MIDI (WP8, WP12) | CoreMIDI directly; or libremidi, RtMidi | Apple SDK; BSD / MIT | Low |
| **AudioUnit hosting + editor windows + sandboxed scanning (WP10, WP11e)** | AudioToolbox `AudioComponent*` / `AVAudioUnit`, editor views as `NSView` (`AUCocoaUIView` / `requestViewController`), own out-of-process scanner (XPC or helper process), state via `kAudioUnitProperty_ClassInfo` | Apple SDK | **High** — the biggest single loss |
| UI components (WP11a–e) | AppKit/SwiftUI (best macOS fit, AU editors embed natively); Dear ImGui; visage; Qt (LGPL) | Apple SDK; MIT; MIT; LGPL/commercial | Medium |
| App/message loop, timers, file chooser (WP3, app) | AppKit run loop / GCD | Apple SDK | Low |

## Recommended free stack (if adopted)

macOS-only, minimal dependencies:

- Engine: CoreAudio (AUHAL) + in-house DSP primitives in `src/engine/dsp/`,
  libsamplerate for resampling, ExtAudioFile for decoding.
- Control: CoreMIDI, oscpack (or tinyosc).
- Plugins: AudioToolbox hosting, out-of-process scanner helper.
- UI: AppKit (Objective-C++ bridge to `src/core`), or SwiftUI with a thin
  C++ interop layer.

Trade-off: Windows/Linux becomes much harder later. If cross-platform
matters, prefer miniaudio + RtMidi/libremidi + Dear ImGui or visage instead
of the Apple-only pieces (but AU hosting stays macOS-only regardless).

## What would have to change

- `CLAUDE.md`: the DSP rule ("never hand-roll, use `juce::dsp`") flips to
  "DSP primitives live in `src/engine/dsp`, each with reference tests"; the
  JUCE fetch and app target in CMake.
- `SPEC-v1.md`: §3 module rules, §5 engine (device layer, smoothing), §13
  plugin hosting, §16 UI.
- `PLAN.md`: WP3 grows (device layer + DSP primitives, probably split into
  WP3a device / WP3b DSP); WP10 roughly doubles (hosting + scanner +
  editors); WP11 rewritten for the chosen toolkit. Estimated net cost: one to
  two extra L-sized work packages.
- Done after WP3 instead of before: every JUCE-using WP (3, 4, 8, 10, 11, 12)
  has to be reworked, so the cheapest moment is before WP3 starts; the next
  cheapest is before WP10/WP11.

## Migration checklist (if revisited)

1. Decide licence goal and target platforms.
2. Update `CLAUDE.md`, `SPEC-v1.md`, `PLAN.md`, `CMakeLists.txt` in one PR.
3. If JUCE code already exists: keep engine interfaces (`AudioEngine`,
   `SpeakerChain`, command queues) and swap implementations behind them one
   WP at a time, with the offline renderer (SPEC §5.7) as the regression
   oracle — render the same project before/after and compare.
