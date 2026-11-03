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
