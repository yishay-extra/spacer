# WP4 — manual validation on a real Mac

CI builds everything and runs every test that needs no audio hardware: the
offline render of file sources against the file (−80 dB, at 48 / 44.1 /
96 kHz), sample-accurate seek and loop, the stalled-reader test (the disk
thread stuck in a read never blocks the audio thread, nor does stopping
it), the background disk thread keeping a file fed, generators, hardware
input channel mapping (with synthetic device buffers), the offline Orbit
render, motion / modulator commands, motion phase across `SwapGraph`, and
"no allocation on the audio path". This checklist covers the rest: a real
device, a real disk, real input channels, and listening.

Record a result (pass / fail plus notes) for each numbered check.

## Setup

Same as `docs/mac-validation/WP3.md` (Setup 1–4): build Release or download the `spacer-engine-check-macOS` artifact, and
define the `check` shell function. Headphones are best; keep the volume low.
Every source here plays at −20 dB except the hardware input (0 dB).

Test files (put them in the current directory):

- `tone48.wav`: a 30 s, 48 kHz stereo file of music or speech you know well.
- `tone44.wav` / `tone96.wav`: the same material at 44.1 kHz and 96 kHz
  (e.g. `afconvert -d LEF32@44100 tone48.wav tone44.wav`, and `@96000`).
- One each of `.aif`, `.flac`, `.ogg`, `.mp3`, `.m4a` (AAC) and an ALAC
  `.m4a` (e.g. `afconvert -f m4af -d alac tone48.wav alac.m4a`).

`check file` prints `file position … s  underruns N` once a second in
addition to the status line.

**Global pass rule:** `xruns`, `dropped` and `underruns` stay 0 in every
check, except check 7, which deliberately starves the disk.

## Checks

| # | Do | Expected | Pass if |
|---|----|----------|---------|
| 1 | `check file tone48.wav --seconds 15` with the device at 48 kHz | The file plays from its start: left channel on the left, right on the right. The position counts up 1 s per second. | Sounds identical to playing the file in QuickTime: no clicks, dropouts or pitch change. |
| 2 | `check file tone44.wav --seconds 15`, then `check file tone96.wav --seconds 15` (device still at 48 kHz) | The same material, resampled on the disk thread. | Same pitch and tempo as check 1, no audible artefacts (no metallic or aliasing sound on cymbals or sibilants). The position advances 1 s per second. |
| 3 | `check file tone48.wav --rate 44100 --seconds 10` and `--rate 96000` | The device opens at the other rate; the file is resampled to it. | Correct pitch at each rate. |
| 4 | `check file <each format file> --seconds 8` | WAV, AIFF, FLAC, Ogg (JUCE) and MP3, AAC, ALAC (CoreAudio) all play. | Every format plays. No `warning: … cannot read audio file` line. |
| 5 | `check file tone48.wav --loop --seconds 70` | After 30 s the file restarts at once and keeps looping; the position wraps to 0. | No gap or click at the loop point (use material whose end and start are both loud, so a gap would be heard). |
| 6 | `check file tone48.wav --seek-at 5 --seconds 12` | At t = 5 s it prints `seek to 0`; playback jumps back to the start after a few ms of silence and continues. | The jump is clean: a short silence, then the beginning of the file. No old audio after the jump. |
| 7 | Copy `tone48.wav` to a slow or removable volume (a USB stick or an SMB share), then `check file /Volumes/…/tone48.wav --seconds 60`. Pull the stick (or disconnect the share) while it plays. | Playback continues from the 1 s buffer; when the medium disappears the file goes silent and `underruns` rises. | `xruns` stays 0 and the status line keeps updating once a second (the audio thread never waits for the disk). Underruns are expected here. |
| 8 | `check generator sine --az -90`, `check generator pink --az 90`, `check generator white`, `check generator impulse` (each `--seconds 8`) | A 440 Hz sine on the left; pink noise on the right (balanced, "rushing"); white noise (hissier); a click every 0.5 s. | Each sounds as described, on the right side. Pink is darker than white. |
| 9 | `check orbit --period 4 --seconds 20` | Pink noise circles the listener clockwise (front → right → behind → left), 4 s per turn. With two speakers at ±90° the front and back halves both sound centred; the image goes hard right, then hard left. | Smooth movement, no zipper noise or steps. The allocation guard (on in this tool) never aborts. |
| 10 | Connect a microphone or a line source to input 1 of the device (or use an aggregate with the built-in mic). `check input --channel 0 --az -90 --seconds 15`, then `--channel 1 --az 90`. Use headphones (feedback!). | The input is heard on the left (then right). Latency is device in + out + 1.5 ms. | The input is clean (no crackle), on the correct side. `--channel 63` (a channel the device does not have) is silent rather than an error. |
| 11 | Activity Monitor during check 1 (or `top -pid`): find the `Spacer disk` thread with `sample <pid> 3`. | Disk reads happen on the `Spacer disk` thread; the audio IO thread only copies from the ring. | No `AudioFormatReader` / `read` frames under the CoreAudio IO thread in the sample. |

## What is not checked here

- The virtual device (`VirtualInput` sources): WP5.
- File transport from OSC (`/source/{id}/file/play|stop|seek`): WP8. The
  commands themselves are tested in CI (`Source files: transport commands`).
- `playOnSceneRecall`: WP9 (scene recall).
- UI: WP11.

## Results — 2026-11-03 (MacBook Air, built-in speakers/mic, macOS 26.6.2)

Test material: synthetic 30 s stereo file (L 440 Hz, R 660 Hz, both
amplitude-modulated at 1 Hz, loud at both ends), converted with `afconvert`
(44.1 k, 96 k, AIFF, AAC, ALAC) and `ffmpeg` (FLAC, Ogg Vorbis, MP3).

**All file checks first failed with `file position 0.000 s` and silence:
the tool's `FilePlay` is queued right after `setProject` and the engine
drops it (#36, root cause confirmed in `EngineProcessor`).** The rows below
were re-run with the tool sending `FilePlay` one tick later (experiment
patch only, not committed).

| # | Result | Notes |
|---|--------|-------|
| 1 | pass (objective) | Position 1 s/s (13.877 s at t=14), 440 + 660 Hz on the mic, xruns 0, underruns 0. L/R placement — **listener**. |
| 2 | pass (objective) | 44.1 k and 96 k files at a 48 k device: position 1 s/s, underruns 0. Artefacts — **listener**. |
| 3 | pass (objective) | Device at 44.1 k and 96 k: plays, underruns 0. Pitch — **listener** (file-mode capture not repeated at those rates). |
| 4 | pass | WAV, AIFF, FLAC, Ogg, MP3, AAC, ALAC all play, no `cannot read` warning. |
| 5 | pass | `--loop` 70 s: position wraps 29.87 → 0.88 at t≈30 and t≈60, underruns 0. Click at the loop point — **listener**. |
| 6 | pass | `seek to 0` printed at t=5, position 4.86 → 0.96 next second. Cleanliness — **listener**. |
| 7 | pass (weak) | File on a 20 MB DMG, `hdiutil detach -force` at t=10: playback continued to the end with underruns 0 and the status line kept updating (the 11 MB file was in the page cache, so the disk never actually stalled). A real USB stick / SMB share is still **open**. |
| 8 | pass (objective) | sine 440 Hz on the mic; white noise flat, pink ~13 dB lower above 1 kHz (darker); impulse broadband. xruns 0. Sides — **listener**. |
| 9 | pass (objective) | 12 s, pink noise, xruns 0, allocation guard silent. Smoothness — **listener**. |
| 10 | pass (objective) | Via the Speakers+Microphone aggregate: channels 0, 1 and 63 run without error or warning, xruns 0 (volume 15 % against feedback). Audible side/cleanliness — **listener with headphones**. |
| 11 | pass | `sample` during check 1: reads under `Spacer disk`; the `com.apple.audio.IOThread.client` stack has no reader frames. |
