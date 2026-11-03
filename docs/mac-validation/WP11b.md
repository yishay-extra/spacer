# WP11b validation — Sources panel (needs a real Mac)

CI covers the logic behind the panel (Linux and macOS, `spacer_tests "[ui]"`):

- the selected-source model (`SourceSelection`: select, reconcile after
  removal / load / undo, neighbour after removal, stepping),
- value formatting and parsing for every unit (dB, degrees, metres, percent,
  ms, Hz, beats / seconds periods), including unit suffixes and bad input,
- the motion parameter table of **every kind** (ids, units, ranges, typed
  values, clamping, rejected values, which fields are live), the Path point /
  Step list operations, step targets, random-walk bounds,
- the input / level / spatializer / distance / Direct / modulator tables and
  their rules (1-based channels, stereo and linked points, mono generators,
  OSC address and target validation),
- add / remove / duplicate (order, names, 64-source limit), every edit as an
  undoable `ProjectEdit` (one slider drag = one undo step, scoped undo),
- the **edit → engine routing**: gain / mute / spread / distance and Manual
  positions are commands for every motion kind; a continuous motion parameter
  of a moving motion is `EngineEffect::MotionParam` → `cmd::SetMotionParam`
  (undo and redo send the model value again); periods, motion kind,
  modulators, inputs, spatializer and add / remove rebuild the graph.

The macOS job also proves the components (`FieldList`, `SourceStrip`,
`SourcePanel`) and the changed `AppContext` / `MainComponent` / `StageView`
compile warning-free. Everything below is visual / interactive / audible and
is checked by hand.

## Setup

- Build the app in **Debug** so the allocation guard is active (SPEC §17):
  `cmake -S . -B build-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build build-debug`.
  Run `build-debug/Spacer_artefacts/Debug/Spacer.app` from a terminal.
- An output device with ≥ 4 channels (or the built-in output; meters still
  show every channel).
- Open `resources/examples/stage-demo.spacer`: quad layout, "Voice" (Manual,
  sine), "Pad" (orbit, pink), "Pair" (stereo, Manual, muted).

## Checklist

| # | Steps | Expected | Result |
|---|-------|----------|--------|
| 1 | Launch with the demo project | The Sources panel is on the left (~340 px) with a list of the three sources and a strip below; the first source is selected; the stage is to its right | |
| 2 | Click "Pad" in the list; then click the "Voice" dot on the stage | The strip follows each click; the clicked source has a blue ring on the stage; selecting in the list highlights the same source on the stage | |
| 3 | Play the project. Watch the list | Every row's small meter moves with its source's level; the strip's meter (under the title) moves too; muted sources show grey | |
| 4 | Select Pad (orbit). Drag **Gain** up and down | Level follows without zipper noise; the orbit **keeps running without a restart or jump** (the dot never snaps back to its start) | |
| 5 | Toggle **Mute** on Pad, then off | Fades out/in over ~10 ms, no click; the orbit keeps running | |
| 6 | Pad: drag **Radius** in the Motion section | The orbit circle on the stage grows/shrinks live and the dot stays on its phase (no restart); Cmd+Z undoes the whole drag in one step, Shift+Cmd+Z redoes it | |
| 7 | Pad: drag **Centre azimuth** / **Phase offset** | Same: continuous, no restart | |
| 8 | Pad: edit **Period** (type `4` then Return; then `2 s`) | Accepted ("4 beats", "2 s"); the orbit rebuilds with the new period (a phase restart is expected here, see PR notes); an invalid entry like `abc` snaps back to the old text | |
| 9 | Double-click a number bar (e.g. Radius) and type `3.5 m` | The value becomes 3.5 m; typing a value past the range clamps it | |
| 10 | Change the **motion kind** of Pad to Line, then Lissajous, Path, Random walk, Step sequence, back to Manual | Each kind shows its own parameter rows; the stage shows the matching trajectory preview; the source does not jump to an unrelated place when switching to Manual / Line | |
| 11 | Path: **Add Point**, edit a point's x/y/z, remove a point with its `x` button; try to go below 2 points | Points add/remove; the second-to-last removal is refused | |
| 12 | Step sequence: add a step, switch its target to **Speaker**, set the speaker id, set a step to **Glide** | Rows appear/disappear accordingly (speaker id vs azimuth/elevation/distance; glide time only for glide) | |
| 13 | Voice: change **Spatializer** VBAP → DBAP → Ambisonic → Direct | Sound re-renders with the algorithm (graph rebuild); for Direct a speaker list with toggles appears, spread/distance disappear, the last speaker cannot be unticked | |
| 14 | Voice: drag **Spread** and the **distance** rows (rolloff k, min radius, low-pass q) | Audible spread/rolloff changes live, no rebuild glitch | |
| 15 | Pair (stereo): toggle **Link points** | "Left" / "Right" buttons appear next to the motion kind; each channel edits its own motion; switching Left/Right shows that channel's parameters | |
| 16 | Voice: **Add modulator** (LFO → target `gain`, depth 3); set the LFO period; add up to 4; the 5th add is disabled | Gain wobbles audibly with the LFO; the `x` button removes a modulator; Cmd+Z restores it | |
| 17 | Add a modulator with source OSC and send `/…` values to it (or MIDI CC) | The OSC address must start with `/`; the target responds to the incoming values | |
| 18 | List toolbar: **+ Add** → Generator, File, Virtual input, Hardware input | A new source appears and is selected. Generator plays a 440 Hz sine at the stage front; File asks for nothing until you press `...` next to the file row and choose an audio file | |
| 19 | **Duplicate** a source; then **Remove** it (button and Delete key) | The copy appears right after the original named "… copy"; remove selects the neighbour; Cmd+Z brings the removed source back in the same list position | |
| 20 | Select a stereo hardware/virtual source; switch **Stereo** off and on | The channel rows change (Channel vs Left/Right channel); channel numbers are 1-based in the UI | |
| 21 | Insert section | Lists the source's insert slots read-only (or "No inserts…"); inserts cannot be edited yet | |
| 22 | Undo/redo through a mix of the above (Cmd+Z several times) | Every edit reverts in order; the strip and the list always match the model; no crash when the selected source is removed or re-added | |
| 23 | Open another project (toolbar Open…) | Selection falls back to the first source of the new project; the strip shows it | |
| 24 | Debug build output | The allocation guard stays silent for all of the above | |

## Known limitations (not failures)

- A period edit rebuilds the graph and restarts the motion phase (SPEC §8
  asks for a re-anchored, continuous position; tracked in WP14).
- Modulator edits rebuild the graph (there is no engine command for them), so
  an LFO's phase restarts.
- Insert slots are read-only until WP10 / WP11e.
