# Spacer.driver — virtual audio device

Placeholder for the CoreAudio **AudioServerPlugIn** that presents a "Spacer"
output device (64 channels) to other applications and hands the audio to the
Spacer app through shared memory. Specified in `docs/SPEC-v1.md` §14, built in
work package WP5 (`docs/PLAN.md`).

This is a separate bundle (`Spacer.driver`), installed to
`/Library/Audio/Plug-Ins/HAL/`, with its own CMake target added here. It must
not depend on JUCE or on `spacer_core`.
