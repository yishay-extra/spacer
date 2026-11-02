# WP8 — manual validation of OSC and MIDI on a real Mac

CI covers everything that needs no hardware: every SPEC §12.1 address is
routed to its event and, through the dispatcher, to its engine command;
malformed packets and arguments are counted; bundle timetags; the 30 Hz
sender bundles; real UDP loopback into `OscServer`; the MIDI mapper (7/14-bit
CC, NRPN, pitch bend, ranges, curves, notes, program change, learn) with
synthetic `MidiMessage`s; and the rule that OSC/MIDI only reach the engine
through `ControlHub::pump()` on the message thread.

This checklist covers real controllers and a second machine. It needs an app
build in which `ControlHub` is wired into the composition root (WP11a) and
`MidiInputService::openAll()` is called; until then run the checks from a
small harness that does the same. Record pass / fail plus notes for each.

## Setup

1. A Mac on macOS 12+, the Spacer app built in Release, a project with two
   sources (ids 1, 2), two speakers, scenes `A`, `B` and a cue list of three
   manual cues; a loop of audio on source 1.
2. A second device on the same network that can send OSC (TouchOSC, or
   `python3 -m pip install python-osc` on a laptop). The Mac's IP is `<mac>`.
3. A MIDI controller with faders, knobs, pads and a pitch wheel (or a
   software one such as the macOS "IAC Driver" plus a MIDI app).
4. Project OSC input port 9000; OSC output enabled to the other device on 9001.

## OSC in (network)

1. From the other device send `/spacer/source/1/gain -12`; source 1 drops 12 dB
   within ~20 ms. Expected: audible and the source panel shows -12.
2. `/spacer/source/1/pos/aed 90 0 2` moves the source to the right;
   `/spacer/source/1/az -90` moves it to the left without changing distance.
3. `/spacer/master/gain -6`, `/spacer/speaker/1/mute 1` (speaker 1 silent),
   `/spacer/panic` (everything ramps to silence).
4. `/spacer/cue/go` three times advances the cue list; `/spacer/cue/back`
   returns; `/spacer/scene/recall A 2.0` recalls over 2 s.
5. `/spacer/clock/bpm 90`, `/spacer/clock/play`, `/spacer/clock/stop`.
6. Send garbage (`echo hello | nc -u -w1 <mac> 9000`) and a wrong-typed message
   (`/spacer/source/1/gain "loud"`). Expected: no crash, no audible change, and
   the OSC malformed counter (status bar, WP11d) increases.
7. Send a bundle with a timetag 2 s in the future (python-osc
   `OscBundleBuilder`). Expected: the change happens about 2 s later, not at
   once. (Message-thread resolution, see PR notes; sample accuracy is not
   expected.)

## OSC out

8. Move a source with an orbit; the other device receives
   `/spacer/source/1/pos/aed` at about 30 Hz (use `oscdump 9001` or TouchOSC's
   monitor). With the transport running it also receives `/spacer/clock/beat`
   and `/spacer/meter/speaker/{id}`; a cue Go sends `/spacer/cue/current`.

## MIDI

9. MIDI learn: start learn on `source.1.gain` (range -60..6), move any fader.
   Expected: the first moved control is captured and the fader then drives the
   gain over its whole range.
10. Learn a knob on `source.1.spread` (0..1) and a log-curve mapping on
    `master.gain`.
11. A pad mapped as a note trigger on a `Midi` cue fires that cue (the cue must
    be next in the list).
12. Set `programChangeChannel` to 1 in the project, send program change 1 on
    channel 1: scene index 1 is recalled with no transition. Channel 2 does
    nothing. Set it back to 0: program changes do nothing.
13. 14-bit CC and NRPN (if the controller supports them): the control sweeps the
    parameter smoothly without 7-bit steps.
14. Save and reopen the project: mappings and `programChangeChannel` come back.
15. Unplug the controller while running: no crash, no stuck notes in the app.
