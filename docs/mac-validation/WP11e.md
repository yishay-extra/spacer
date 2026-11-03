# WP11e validation — Plugin UI: editor windows, speaker inserts, scanning (needs a real Mac)

CI covers the logic behind the UI (Linux and macOS, `spacer_tests "[ui]"` and
the engine tests, with WP10's fake "SpacerTest" plugin format):

- slot <-> window bookkeeping (`WindowBook`): one window per slot, source and
  speaker slots are distinct, a window follows the instance behind its slot
  across graph swaps and recalls, and closes when the slot, the speaker or the
  plug-in goes away — also against a real `PluginHost` registry;
- speaker-chain insert edits (add / remove / bypass, limit of 4, undo/redo, the
  chain keeps the speaker's trim/delay/polarity, the engine gets
  `SetInsertBypass{Speaker}`), and layout edits dropping/restoring the chains
  of removed speakers;
- the background scan (`PluginScanJob`): runs off the calling thread, progress
  and cancel, partial results kept and saved, rescan, the child-process
  scanner path with a crashing plug-in blacklisted and listed, and no
  on-demand scanning while a scan runs;
- scan text / button states and blacklist rows;
- "Update scene" / "New scene" capture plug-in state first (ordering test and an
  end-to-end test with a live plug-in).

The macOS job also proves the new components (`PluginUi`, `PluginEditorWindow`,
`PluginManagerWindow`, `InsertSlotsView`, the speaker inspector, the
refactored source strip) compile warning-free against the full JUCE GUI
modules. Windows, real Audio Units and everything visual or interactive is
checked by hand below.

## Setup

- Build the app in **Debug** (allocation guard active, SPEC §17) and run it from
  a terminal: `cmake -S . -B build-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build build-debug`,
  then `build-debug/Spacer_artefacts/Debug/Spacer.app`.
- Apple's own effect Audio Units are enough: **AUDelay**, **AUGraphicEQ**,
  **AUPeakLimiter**, **AUDynamicsProcessor** (category Effects, vendor Apple).
  If you have a third-party AU with a custom UI, use it for rows 6 and 7.
- Open `resources/examples/stage-demo.spacer` and use the built-in output
  (channels 1-2 audible) or a 4-channel device.

## Checklist

| # | Steps | Expected | Result |
|---|-------|----------|--------|
| 1 | Select a source, in its strip press **Plug-ins...** | Plug-ins window opens (not modal); lists nothing yet, "0 plug-ins, 0 blacklisted" | |
| 2 | Press **Scan** | Progress bar and "Scanning n of N: <name>" advance; the main window stays responsive (stage keeps animating, audio keeps playing, you can move the window and press GO); at the end "Scan finished: n added, … up to date" and the Apple AUs appear sorted by name | |
| 3 | Press **Scan** again right away | Finishes at once, everything "up to date", 0 added | |
| 4 | Press **Rescan all**, then **Cancel** after a second | Status "Cancelling after the current plug-in...", then "Scan cancelled: …". Plug-ins scanned so far stay in the list. **Scan** afterwards finishes the rest | |
| 5 | (Only with a misbehaving AU installed) **Rescan all** | App never crashes or hangs; the offending AU shows in "Blacklisted" with the reason (crashed / timed out / failed). Select it, **Retry selected** scans just that one again. Quit and relaunch: it is still blacklisted until retried | |
| 6 | Source strip: **Add insert...** → AUDelay | A row "1  AUDelay" with **Bypass**, **Edit**, **X**. Sound gains the delay | |
| 7 | **Edit** on that row | A window "AUDelay - <source>, insert 1" opens with the AU's own UI (or JUCE's generic editor for an AU without one). Moving a control changes the sound. **Edit** again only raises the same window; closing it with the red button works and the AU keeps running | |
| 8 | Add a second insert (AUGraphicEQ), open both editors | Two windows, one per slot, titles tell them apart | |
| 9 | With an editor open: toggle **Bypass** in the strip | Sound crossfades (no click); the editor window stays; toggle back | |
| 10 | With an editor open: remove another source's / this source's insert with **X** | Removing this slot closes its window; removing a different slot leaves the window open | |
| 11 | Change a setting in the editor (e.g. delay time), then Cues panel → select a scene → **Update**; change the knob again; **Recall** that scene | After Recall the knob is back at the value stored by Update (editor edits are captured by Update and restored by Recall); the open editor window shows the restored value | |
| 12 | Same as 11 using **New** instead of **Update**, then recall it | Same result | |
| 13 | Change a setting in an editor, **Save**, quit, relaunch, open the project | The plug-in comes back with that setting | |
| 14 | Open an editor, then Go to a cue whose scene has other state for that slot | The editor window stays open and shows the recalled state (it followed the new instance) | |
| 15 | Open an editor, then Cmd+Z the insert's "Add Insert" | Editor window closes, no crash | |
| 16 | Open an editor, then open another project (toolbar **Open...**) | Editor window closes (or follows if the new project has the same plug-in in that slot); no crash | |
| 17 | Layout tab: select speaker 1 | Below the table "Speaker 1 - inserts": same view as the source strip | |
| 18 | **Add insert...** → AUPeakLimiter on speaker 1; **Edit** | Insert row, editor window titled "… - Speaker 1, insert 1". Audio on speaker 1 is limited. Trim/delay/polarity of that speaker in the table are unchanged | |
| 19 | Toggle **Bypass** on the speaker insert, then Cmd+Z / Shift+Cmd+Z | Sound switches with a short crossfade; undo/redo move the checkbox and the sound | |
| 20 | Remove the speaker's last insert, then change its **Trim dB** in the table | Trim takes effect (the empty chain is dropped, so the layout value applies) | |
| 21 | Add inserts to speaker 3, remove speaker 3 from the layout, then Cmd+Z | Speaker, its insert and its open editor window go with the removal (project still saves/loads), and come back with Undo | |
| 22 | Open a project whose insert names a plug-in that is not installed | Row shows "(missing)" in orange, **Edit** is disabled, the sound passes through; no crash | |
| 23 | Add 4 inserts to a source or speaker | **Add insert...** is disabled at 4 | |
| 24 | Quit while a scan is running | App quits within a few seconds (waits for the plug-in being scanned), no crash report | |

Not covered (known limits, see the PR notes): a scan cannot be interrupted in
the middle of one plug-in (cancel takes effect after that plug-in finishes or
times out, at most 30 s); plug-in edits do not mark the project as modified
until something captures them (save, scene update, recall).
