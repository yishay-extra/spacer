# Mac validation — handoff (2026-11-03)

**Request (verbatim):** "work from docs/mac-validation/README.md"

## Rules in force
- Run order and recording rules: `docs/mac-validation/README.md`. Results go
  in each WP file; failures become issues referencing WP and check number.
- One build at a time on this Mac; `ctest` must be green before "done".
- Never relax a check to make it pass.

## State
- Local Release (`build/`) and Debug (`build-debug/`) builds exist in the
  main checkout; ctest 723/723. Tool binary:
  `build/spacer_engine_check_artefacts/Release/spacer-engine-check` (#38).
- Done and recorded: WP3 (all 10 checks, objective part), WP4 (all 11,
  objective part), WP5 check 1. Results sections appended to the WP files.
- Issues filed: #36 (FilePlay dropped after SwapGraph — engine, confirmed
  root cause), #37 (pitch wrong / silence after live rate change), #38
  (tool name in docs), #39 (status bar / Install button text), #40 (20
  clang warnings on Xcode 27.2), #41 (`Untitled *` at launch).

## What needs the owner (in run order)
1. **Decide #36** (how commands after a swap should be ordered) — it blocks
   every file/transport check from being meaningful in the app, and the
   tool's WP4 rows were measured with a one-tick delay patch (patch text in
   the issue).
2. **WP5 checks 2–13**: click Install, enter the admin password; then
   checks 5–10 need ears (tone on the left, isolation, drift 10 min), 11 a
   DAW, 12–13 `sudo`. If coreaudiod refuses the ad-hoc-signed bundle, see
   Setup 4 in WP5.md.
3. **Listener columns** in WP3/WP4 results (L/R placement, zipper noise,
   loop click, pink vs white by ear). ~10 minutes with headphones:
   `check tone`, `check tone --az 90`, `check pan --seconds 20`,
   `check file tone48.wav --loop --seconds 70` (after #36 or with the patch).
4. WP3 check 5 (−6 dB / 140 samples) and WP4 check 7 (real slow medium)
   need a second interface / a USB stick.
5. WP10, WP12, WP8, WP9, UI checklist, WP14: not started (need plug-ins,
   a DAW, a controller, an OSC sender, and a listener).

## Helpers (scratchpad, not in the repo)
- `setrate.swift`: CoreAudio nominal-rate switcher (`setrate "<device>" 96000`).
- `rec.swift`: AVAudioEngine mic recorder (`rec out.wav 4`); `peaks.py`:
  merged spectral peaks. ffmpeg's avfoundation capture drops frames and
  reads ~1 % low — do not use it for pitch.
