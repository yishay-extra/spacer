# WP11a validation — Stage view + layout editor (needs a real Mac)

CI covers the logic behind the UI (Linux and macOS, `spacer_tests "[ui]"`):
stage world↔screen transforms for the top and side view, zoom, hit-testing,
the drag rules (Manual position vs. motion centre, stereo pairs, linked
points), trajectory previews, every layout-editor operation and table field
parser, the compensation display, undo/redo (including one-drag-one-step and
scoped undo), the edit → engine routing (commands vs. graph rebuild), the
speaker-test overlay project and the meter ballistics. The macOS job also
proves the app (components + composition root) compiles warning-free.

Everything below is visual / interactive / audible and is checked by hand.

## Setup

- Build the app in **Debug** so the allocation guard is active (SPEC §17):
  `cmake -S . -B build-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build build-debug`.
  Run `build-debug/Spacer_artefacts/Debug/Spacer.app` from a terminal so
  guard assertions are visible.
- An output device with ≥ 4 channels (or the built-in output: only channels
  1–2 are audible then; the meters still show all four).
- Open `resources/examples/stage-demo.spacer` (toolbar **Open...**): quad
  layout, "Voice" (Manual, sine), "Pad" (orbit, pink), "Pair" (stereo,
  Manual, muted), cue list "Demo" (A, B, loop).
- Pink / sine generators need WP4's live generator inputs; before WP4 is
  merged the stage and meters work but sources and the speaker test are silent.

## Checklist

| # | Steps | Expected | Result |
|---|-------|----------|--------|
| 1 | Launch; resize the window as small as it goes | One dark window; it does not get smaller than 1100×700 | |
| 2 | Stage tab, top view | Listener head at the centre, nose up (front). FL top-left, FR top-right, RL/RR below; 1 m rings labelled | |
| 3 | Look at Pad | A faint closed circle (its orbit) with the Pad dot moving along it | |
| 4 | Press **GO** (or space) twice | Cue A then B fires; on B Voice glides to the right over 4 s and Pair appears (unmuted); title/status show the armed list | |
| 5 | Drag Voice around the listener | The dot follows the mouse; the sound pans with it without zipper noise; the speaker rings show where the signal goes | |
| 6 | Release, press Cmd+Z, then Shift+Cmd+Z | Undo puts Voice back where the drag started in **one** step; redo moves it to the drop point again | |
| 7 | Drag Pad (orbiting) | The orbit circle moves with the mouse (centre offset); the sound follows within ~0.1 s; after release the orbit continues from the new centre | |
| 8 | Drag either dot of Pair | Both points move together, keeping their width | |
| 9 | Click **Side view** | Front is to the right, up is up; speakers on the floor line; dragging Voice up raises its elevation (audible only on a 3D layout: apply 7.1.4 first) | |
| 10 | Mouse wheel over the stage; double-click empty space | Zooms about the mouse position; double-click fits the layout again | |
| 11 | Click a speaker (no drag) | Orange outline + "Speaker test" banner; pink noise from that speaker only; all sources silent | |
| 12 | Click the same speaker again; then click another one | Test stops and the show sound comes back; clicking another speaker moves the test there | |
| 13 | Play loud material (raise a source gain) | Meter rings grow with level, turn yellow above −6 dBFS and red while the limiter reduces gain | |
| 14 | Esc; Esc again | First Esc silences all outputs (button shows PANIC ON); the second releases it | |
| 15 | Layout tab | Table lists the 4 speakers with az/el/dist/trim/delay/pol/out/LFE and read-only Comp ms / Comp dB / Total columns; the stage on the right shows speakers only | |
| 16 | Double-click FL's Dist, type 1 and Enter | Comp ms of FL becomes ≈ 2.92 ms and Comp dB ≈ −6.02; FL moves inwards on the stage; Cmd+Z restores it | |
| 17 | Type `abc` into an Az cell | The old value comes back; no undo step is added | |
| 18 | Set FL's Out to 3 | FL now plays on device channel 3 (check with the speaker test / Test button) | |
| 19 | Choose preset 7.1.4, **Apply preset**; Cmd+Z | 12 speakers incl. LFE (drawn round, purple); undo brings the quad back | |
| 20 | Add, Duplicate (with a row selected), Remove | New rows with fresh ids and the next free output; Remove is disabled at 2 speakers | |
| 21 | **Assign outputs 1..N** | Out column becomes 1, 2, 3 … in table order | |
| 22 | Drag a speaker on the layout stage | It moves; the table updates live; the whole drag is one undo step; no dropouts while dragging | |
| 23 | Click a speaker on the layout stage | Its table row is selected | |
| 24 | Press **Test** in a row, then **Stop** | Same as #11/#12 for that speaker | |
| 25 | Save As…, edit something, Save, reopen | Title shows `*` while unsaved; the reopened project matches | |
| 26 | Throughout #5–#24 (Debug build) | The allocation guard never fires; no xruns counted in the status line | |
| 27 | Installation mode (also WP9 validation #2, which needed this composition root): write `~/Library/Application Support/Spacer/installation.json` with `{"enabled": true, "lastProjectPath": "<path to stage-demo.spacer>", "defaultCueList": "", "autosaveSeconds": 10, "suppressDialogs": true}`, launch | The demo opens, the list is armed and cue A fires without any dialog; after an edit `stage-demo.autosave.spacer` appears within ~10 s | |
