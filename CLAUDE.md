# Spacer — notes for agents working in this repo

Spacer is a macOS spatial-audio app (C++20 + JUCE). Read these before coding:

- `docs/SPEC-v1.md` — **the contract.** Conventions, limits, interfaces,
  formulas, file format, OSC address space. Do not change a decision in the
  spec inside a work package; if it is wrong, say so in your report.
- `docs/PLAN.md` — work packages (WP). Each is self-contained with acceptance
  criteria. Do only the WP you were given.
- `docs/DESIGN.md` — background and rationale.

## Layout

```
src/core/      pure C++20, NO JUCE, NO platform APIs. Model, maths,
               spatializers, motion, clock, project file. Testable anywhere.
src/engine/    JUCE audio engine: device I/O, source graph, speaker chain,
               command queues, offline renderer.
src/control/   OSC, MIDI, cue engine, external clocks, plugin hosting glue.
src/ui/        JUCE UI components.
src/app/       Application entry point and main window.
driver/        Spacer.driver (CoreAudio HAL plugin). No JUCE, no spacer_core.
tests/         Catch2 tests for src/core (and offline-render tests later).
```

Dependency rule: `core` depends on nothing in-repo. `engine`/`control`/`ui`
depend on `core`. `core` must never include JUCE headers.

## Build and test

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release   # add -DSPACER_BUILD_APP=OFF on non-macOS
cmake --build build
ctest --test-dir build --output-on-failure
```

JUCE and other deps are fetched by CMake on first configure. The app target
only builds on macOS; `spacer_core` and `spacer_tests` build on Linux/macOS.
Before finishing a WP, the build must be warning-free and all tests must pass.

## Real-time rules (audio thread)

Anything reachable from the audio callback — `Spatializer::render`,
`Motion::evaluate`, `ClockSource::advance`, engine `process` — must not:
allocate, free, lock a mutex, do I/O, log, call into the UI, or call plugin
scanning. Parameters cross threads only via the lock-free queues in
`src/engine`. Prepare/allocate in `prepare()`; process in `render()`.

## Conventions

- Namespace `spacer`. Files `PascalCase.h/.cpp`, one class per file.
- `clang-format` (config in repo) before committing.
- Tests: `tests/<Thing>Tests.cpp`, Catch2 v3, one `TEST_CASE` per behaviour.
- Coordinates: see `src/core/Position.h` header comment. Never add a second
  convention.
- Gains in dB in the model, linear in the engine; convert at the boundary.
- Every new model type gets JSON (de)serialisation and a round-trip test.
- Commit messages: short imperative subject, body explains *why*.
