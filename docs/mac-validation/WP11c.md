# WP11c validation — Cues panel + scene manager (needs a real Mac)

CI covers the logic behind the panel (Linux and macOS, `spacer_tests "[ui]"`
and the engine tests): trigger text formatting/parsing for every trigger
kind, cue add / remove / duplicate / reorder, every cue field parser, cue-list
create / rename / delete / duplicate, scene capture / update / rename (cues
follow) / delete, the "modified" marker, undo/redo of show edits (scoped to
scenes + cue lists), the current/next highlight state and header texts,
`CueSequencer::updateList`, `CueEngine::refreshArmedList` and the recall
counter behind the OSC/MIDI notification fix. The macOS job also proves the
panel, scene manager and the `MainComponent` placement compile warning-free.

Everything below is visual / interactive / audible and is checked by hand.

## Setup

- Build the app in **Debug** (allocation guard active, SPEC §17) and run it
  from a terminal: `cmake -S . -B build-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build build-debug`,
  then `build-debug/Spacer_artefacts/Debug/Spacer.app`.
- Open `resources/examples/stage-demo.spacer` (toolbar **Open...**): scenes A
  and B, cue list "Demo" (A, B, loop). Use a 4-channel output or the built-in
  one (channels 1-2 audible).
- For the OSC rows use any OSC sender (e.g. `oscsend localhost 9000 ...`);
  addresses are in SPEC §12.1.

## Checklist

| # | Steps | Expected | Result |
|---|-------|----------|--------|
| 1 | Launch, look at the right edge | Cues panel (≈400 px) right of the Stage/Layout tabs: list selector, Arm/New/Rename/Delete, Back / GO / Jump to / Loop, cue table, Add/Duplicate/Remove/Up/Down, scene manager below. Nothing overlaps at 1100×700 | |
| 2 | Before any Go | Cue table shows "Demo" with cue 1 in amber (next, ring marker); header says "No list armed … Press Arm" | |
| 3 | Press **Arm**, then **GO** (or space) | List shows "(armed)"; cue 1 turns green (filled dot), cue 2 amber; texts read "Cue 1 of 2: …" / "Next: … (manual)"; sound moves to scene A | |
| 4 | Press **GO** twice more | Cue 2 green; with Loop on the next is cue 1 again (amber); with Loop off, "End of list" and Go does nothing | |
| 5 | **Back** | Previous cue fires and highlights move back | |
| 6 | Select a cue, **Jump to**; double-click another cue's number | That cue fires immediately; highlights follow | |
| 7 | Double-click a cue's Name, type, Enter | Name changes; Cmd+Z restores it, Shift+Cmd+Z re-applies | |
| 8 | Edit **Time** to `4`, `abc`, `-1` | `4` accepted; invalid values snap back to the previous value | |
| 9 | Edit **Trigger** to `after 3 s`, `bar 2 beat 1`, `osc /go/b`, `note 60 ch2`, `22:00 mon-fri`, `nonsense` | Valid ones are reformatted (e.g. `after 3 s`, `osc /go/b`); `nonsense` snaps back | |
| 10 | Make cue 2 `after 3 s`, Go to cue 1 | Cue 2 fires by itself ≈3 s later; highlights follow without touching the mouse | |
| 11 | Edit a cue of the *armed* list while it is running (rename, change its trigger) | Highlights and countdown keep their position (no reset to "not started") | |
| 12 | Scene column: pick another scene in a cue's combo; Restart toggle (R) | Cue recalls that scene next time; undo works | |
| 13 | Add / Duplicate / Remove / Up / Down | Rows appear/disappear/reorder, selection follows, each is one undo step | |
| 14 | New / Rename / Delete cue list | Prompt appears; list selector updates; renaming the armed list keeps it armed and in place; deleting it disarms | |
| 15 | Scene manager: change a source's gain (e.g. drag it on the stage), look at "Live:" | "Live: A (modified)" in amber; the scene row shows an amber dot | |
| 16 | **Update** with that scene selected | Marker clears; Cmd+Z brings the old scene back and the marker returns | |
| 17 | Modify live again, **Revert** | The live scene is recalled over the Time value; marker clears | |
| 18 | **New**, name it | Scene appears, selected and live; rename it: cues using it follow (check their Scene column) | |
| 19 | **Delete** a scene used by a cue | Scene disappears; the cue's combo shows `<name> (missing)`; Go on that cue still advances the list (no crash) | |
| 20 | Double-click a scene | It recalls over the Time value; "Live:" follows | |
| 21 | Send OSC `/spacer/scene/recall "B" 1.0` (SPEC §12.1) for another scene while a cue is current | Sound changes **and** "Live:" updates (this used to be invisible to the UI) | |
| 22 | During a long transition press Cmd+Z / edit cues | No audio glitches or clicks; no allocation-guard assertion in the terminal | |
