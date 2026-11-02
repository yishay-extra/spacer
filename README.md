# spacer

A macOS spatial-audio engine for arbitrary speaker arrays: takes audio from
DAWs (via a virtual device), hardware inputs and files, places each source in
space over 2–32 speakers (VBAP / DBAP / ambisonics) with automatic room
compensation, and moves sources by hand, by programmed trajectories, or in
clock-synced step sequences. Built for live performance and installations.

## Documents

- [`docs/DESIGN.md`](docs/DESIGN.md) — architecture and rationale
- [`docs/SPEC-v1.md`](docs/SPEC-v1.md) — the v1 contract: conventions, interfaces, formulas, file format, OSC
- [`docs/PLAN.md`](docs/PLAN.md) — work packages and status
- [`CLAUDE.md`](CLAUDE.md) — rules for agents/contributors

## Build

Requires CMake ≥ 3.25, Ninja, and on macOS Xcode 15+. Dependencies (JUCE,
Catch2, nlohmann/json) are fetched by CMake.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

On Linux pass `-DSPACER_BUILD_APP=OFF`: the JUCE-free core library and its
tests build and run anywhere; the app itself is macOS-only.

## Status

Phase 0 (scaffold) — see `docs/PLAN.md`.
