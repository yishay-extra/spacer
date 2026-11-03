# Open issues #37–#41 — plan (2026-11-03)

**Request (verbatim):** "pick up an issues and work on them"

Spec: `docs/SPEC-v1.md` (binding). Rules: `CLAUDE.md` (repo) and the global rules.

## Global Constraints

- One worktree, one build dir: `~/projects/spacer.worktrees/issues-2026-11-03`,
  `build/` (Release, Ninja, already configured). One build at a time on this Mac.
- Each task is its own branch off `issues-2026-11-03-base` (= origin/main + this plan),
  created in this worktree with `git checkout -b <branch> issues-2026-11-03-base`. Tasks run sequentially.
- Never `git stash`, `reset`, `checkout --`, or `clean` the tree. Leave no background
  processes. Do not spawn subagents.
- `src/core` never includes JUCE. Audio-thread code never allocates, locks, logs.
- Gate before DONE: `cmake --build build 2>&1 | grep -c 'warning:'` is 0 and
  `ctest --test-dir build --output-on-failure` is green. Never relax a test, check,
  or threshold; write the proposed change in the report and stop.
- `clang-format -i` on every C++ file touched (config in repo).
- Comments: default none; only constraints, traps, measured numbers, invariants.
  No issue numbers, dates, or history in code comments.
- Commit: short imperative subject, body says why, last line
  `Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>`. Do not push.
- Do not edit `docs/SPEC-v1.md` or `docs/PLAN.md`.

## Task 1 — #38 + #39: tool binary name, status bar text (branch `fix-38-39-tool-name-status-text`)

Ruling (#38): fix the target, not the docs. `juce_add_console_app` ignores
`PRODUCT_NAME` for the binary name (JUCE 8.0.15 `JUCEUtils.cmake`: console apps never
call `_juce_set_output_name`). Add
`set_target_properties(spacer_engine_check PROPERTIES OUTPUT_NAME spacer-engine-check)`
after the `juce_add_console_app` call in `CMakeLists.txt`; drop the now-redundant
`PRODUCT_NAME` argument. Verify by building and `ls build/spacer_engine_check_artefacts/Release/`
= `spacer-engine-check`. Confirm `.github/workflows/ci.yml` uploads the artefacts
directory (it does) so no CI change is needed. Update
`docs/superpowers/plans/2026-11-03-mac-validation-handoff.md` line naming the binary.

Ruling (#39): spec §"Status bar" (`docs/SPEC-v1.md` ~line 562) lists CPU %, xruns,
latency, clock, driver status — no output count. So WP3 check 9 is wrong: edit
`docs/mac-validation/WP3.md` check 9 to expect "device name, rate / buffer, CPU,
xruns, latency, clock (bpm, bar.beat)" and remove "`2 out` or more". The Install
button: spec line ~490 names the action "Install virtual device…", `SettingsPanel`
uses that text, and `src/ui/StatusBar.h` line 15 documents the offer as
"Install virtual device...". Change `src/ui/StatusBar.h` button text to
`"Install virtual device..."`; WP5 check 1 stays. Check for a test that asserts the
old label and update it. If the status bar's layout gives the button a fixed width
that truncates the longer label, widen it (read `StatusBar.cpp` `resized()`).
Also update the WP3 results row for check 9 ("text differs, #39") to "pass" is NOT
required — leave results rows alone.

Acceptance: build warning-free, ctest green, binary named `spacer-engine-check`,
`grep -rn 'spacer_engine_check' docs/` only matches the artefacts directory path.

## Task 2 — #41: Untitled project marked modified at launch (branch `fix-41-untitled-dirty`)

`src/app/AppContext.cpp:326` sets `savedRevision_ = revision_ + 1` with the comment
"a fresh project is not dirty", while `isDirty()` is `revision_ != savedRevision_`
(`AppContext.h:123`). Find what increments `revision_` after that line at launch
(something was expected to bump it once; either it no longer does, or does so
conditionally). Fix the root cause so an untouched Untitled project is not dirty,
and so a genuine first edit still makes it dirty. Prefer making the saved
revision explicit (e.g. mark clean after the fresh project is fully constructed)
over the `+1` guess.

Tests: add a Catch2 test (find where `AppContext` is tested; if it is not unit
testable, test the smallest testable piece and verify the title manually in the
Release app: launch `build/Spacer_artefacts/Release/Spacer.app`, read the window
title via `osascript -e 'tell application "System Events" to get name of window 1 of process "Spacer"'`,
quit it with `osascript -e 'quit app "Spacer"'`). Plant the defect (restore the `+1`)
and show the test goes red, then restore.

Acceptance: title is `Spacer - Untitled` at launch and `Spacer - Untitled *` after
one edit (a test proves both); build warning-free; ctest green.

## Task 3 — #40: 20 clang warnings with Xcode 27.2 (branch `fix-40-clang-warnings`)

Reproduce first: `cmake --build build --clean-first 2>&1 | grep 'warning:' | sort | uniq -c`
and record the list in the report (the baseline build log at
`build-baseline.log` in the worktree root has them too). Fix each in code, never by
suppression or by changing warning flags:
- `-Wfloat-equal` in `src/core/Cue.h`, `src/core/MotionSpec.h`: core is JUCE-free,
  so add one small header helper in `src/core` (e.g. `exactlyEqual(double, double)`
  implemented without `==` on floats triggering the warning — e.g. via
  `std::memcmp`-free approach such as `!(a < b) && !(b < a)`), and use it in the
  `operator==` bodies. If such a helper already exists in core, use it.
- `-Wshadow-uncaptured-local` in `src/ui/TextPrompt.h`: rename the shadowing local.
- `-Wswitch` in `tests/SceneDiffTests.cpp`: handle `SourceMotionParam`.
- `-Wimplicit-int-float-conversion` in `tests/engine/AmbisonicEngineTests.cpp`: explicit cast.

Acceptance: `cmake --build build --clean-first 2>&1 | grep -c 'warning:'` prints 0;
ctest green; no new `#pragma`, no flag changes in CMake.

## Task 4 — #37: wrong pitch / silence after live sample-rate change (branch `fix-37-live-rate-change`)

Use systematic debugging: reproduce, root-cause, then fix. Read the issue body
(`gh issue view 37`) and `docs/mac-validation/WP3.md` check 6 and its result row.

Hardware reproduction (this Mac, built-in output + built-in mic). Helpers in
`/private/tmp/claude-501/-Users-yishay-projects-spacer/5ee240d1-4515-4494-8990-13b94b5f664b/scratchpad/tools/`:
`setrate "<device name>" <hz>` (compiled; source `setrate.swift`), `rec out.wav <seconds>`
(mic recorder, compiled; source `rec.swift`), `peaks.py <wav>` (spectral peaks, run with
`uv run --with numpy<3 --with scipy<2 python -I peaks.py …` or as its header says).
Procedure: `build/spacer_engine_check_artefacts/Release/spacer-engine-check tone --seconds 40`
(binary is `spacer_engine_check` if Task 1 is not merged into this branch — check `ls`)
in the background, record 4 s windows with `rec` while calling `setrate` 48000 →
44100 → 96000 → 48000 about 8 s apart; measure the peak each window. Before the fix
expect 880/404 Hz and silence; after the fix expect 440 Hz in every window and no
silence. Restore the device rate to 48000 at the end. Kill the tool if it hangs;
leave no processes.

Two suspects to separate (issue body): (a) the tool's tone is a `BufferInput`
generated at the opening rate and not regenerated on restart
(`tools/EngineCheck.cpp` `playTone`); (b) the engine: `AudioEngine::audioDeviceAboutToStart`
→ `EngineProcessor::setGraphWhileStopped` / `adoptSampleRate` / `takeStateFrom`
(`src/engine/EngineProcessor.cpp`, `src/engine/Graph.cpp`, `BufferInput`, `FilePlayer`,
`GeneratorInput`), and whether inputs are re-prepared at the new rate. The silence
after the third change is an engine (or device-layer) bug until proven otherwise.

Fix both sides that are actually wrong. For the engine side, add a Catch2 test in
`tests/engine/` that drives `EngineProcessor` (or `AudioEngine` with a fake
`juce::AudioIODevice` if one exists in the tests) through 48 k → 44.1 k → 96 k → 48 k
restarts with a source playing and asserts the output keeps the generator's
frequency and does not go silent — the test must be red before the fix. If the
tool's tone should be a `GeneratorInput` (resampling-free, rate-independent), use it.
Keep the spec's conventions; if the spec is wrong, say so in the report.

Acceptance: hardware measurement table in the report (rate, measured Hz, level) for
before and after; engine test red-then-green; build warning-free; ctest green.
