# WP9 validation (needs a real Mac and audio device)

Everything in WP9 is covered by offline tests on CI except the items below.
The app has no composition root yet (engine, CueEngine and UI are not wired in
`src/app/`), so the installation-mode launch sequence cannot be exercised end
to end until that exists; `installation::autoload`, `startShow` and `Autosaver`
are unit tested.

| # | Setup | Steps | Expected | Result |
|---|-------|-------|----------|--------|
| 1 | `spacer-engine-check` or the app with a project with an `AtBeat` cue (bar 2 beat 1), 120 bpm, internal clock, quantised start | Play; record the speaker output | The scene change starts exactly at beat 4 (2.000 s after the transport start), no click | |
| 2 | Cue list with `loop`, `AfterPrevious` 5 s ±2 s cues, installation mode on | Launch the app | Opens the last project, arms the list, fires cue 1 with no dialog, cycles for 30 min without gaps | |
| 3 | Same, kill the app mid-show (`kill -9`) | Relaunch (manually until WP13's LaunchAgent) | Last project (or `*.autosave.spacer` if the project is unreadable) loads and the show restarts | |
| 4 | Large project (64 sources) | Recall a scene that adds/replaces sources | Graph build on the message thread stays < 10 ms of UI-blocking work or is moved to a worker; no xruns | |
