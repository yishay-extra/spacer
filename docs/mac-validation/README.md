# Mac validation — run order

The build-out phase had no real Mac. Each macOS work package passed Linux and
macOS CI; everything that needs real hardware or a listener was assumed
correct and written down as a checklist here (see "Phase decisions" in
`docs/PLAN.md`). Run them in this order on a real Mac; earlier ones unblock
or de-risk later ones. Download the CI artifacts (`Spacer-macOS`,
`Spacer-driver-macOS`, `spacer-engine-check`) or build locally.

1. **WP5.md — Spacer.driver.** Biggest open risk first: whether coreaudiod's
   sandbox allows `shm_open` of the driver's ring, and whether an ad-hoc-signed
   HAL plug-in loads. If shm is refused, the transport has to move to a mach
   shared-memory handle (SPEC §14) before anything else that uses VirtualInput.
2. **WP3.md — audio engine** (via `spacer-engine-check`): device output,
   panning by ear, compensation delay, rate/buffer changes, unplug, aggregate.
3. **WP4.md — sources:** files (incl. MP3/AAC/ALAC), generators, hardware
   inputs, disk stalls.
4. **WP10.md — AudioUnit hosting:** third-party AUs, crashy plug-ins in the
   scanner, latency-reporting plug-ins.
5. **WP12.md — external clocks** against a DAW (MIDI clock, MTC, OSC; Link only
   in a `-DSPACER_WITH_LINK=ON` build).
6. **WP8.md — OSC and MIDI** from another device / real controllers.
7. **WP9.md — scenes, cues, installation mode** (30-minute run, relaunch).
8. **UI: `docs/ui-checklist.md`** → WP11a (stage, layout editor), WP11b
   (sources panel), WP11c (cues panel), WP11d (settings, status bar), WP11e
   (plug-in editors, scanning).
9. **WP14.md — integration pass:** OSC timetag timing, clock latency trim,
   live edits without phase jumps, file crossfade on recall, ambisonic vs VBAP
   level on a real ring.

Record results in each file (pass/fail + notes); file anything that fails as
an issue referencing the WP and check number.
