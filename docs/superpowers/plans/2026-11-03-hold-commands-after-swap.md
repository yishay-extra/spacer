# Hold commands queued after a SwapGraph until the swap applies (#36)

Spec: `docs/SPEC-v1.md` §5.2 (one FIFO, single producer, timed commands,
structural changes via `SwapGraph`). Issue: https://github.com/yishay-extra/spacer/issues/36
Decision (owner, 2026-11-03): option 3 — hold and replay, exact FIFO semantics.

## Problem

`EngineProcessor::applyCommands()` drains the whole FIFO. `cmd::SwapGraph`
only sets `pending_`; the swap lands later in `process()` (`applyPendingSwap`,
possibly after a one-block fade-out, or later still while the retire queue is
full or an ambisonic crossfade runs). Every command popped after the swap in
the same drain is applied to `graph_` — the previous graph, or no graph —
and is silently lost when it targets a source that only the new graph has
(or when `graph_ == nullptr`). Reproduced on a Mac: `spacer-engine-check file`
sends `FilePlay{1}` right after `setProject`; the file never starts.

## Global constraints

- Audio-thread code (`process`, `apply*`, `applyCommands`, `insertTimed`,
  `applyTimedUpTo`): no allocation, no locks, no I/O, no logging. Fixed-size
  members only. `AllocationGuard` runs in the engine tests.
- Command order is the FIFO order. A command queued after a `SwapGraph` acts on
  the graph that swap installs (or a later one), never on the graph it replaces.
- A `SwapGraph` is never held: it still supersedes an unapplied pending graph.
- Overflow policy unchanged: a full FIFO drops on the producer side and counts
  (`droppedCommands()`); the hold must not add a second drop path.
- `setGraphWhileStopped()` keeps draining *everything* queued before it (its
  comment explains why: a `SwapGraph` built for the old device must not replace
  the device's new graph later). Audio-thread holding must not change that.
- Timed commands (`pushAt`) that come due while a swap is pending are held
  too, and applied, still in sample order, once the swap has landed. Late by
  the swap wait, never lost, never applied to the old graph.
- Build warning-free (the 20 pre-existing clang warnings in #40 are not yours;
  add none); `clang-format` (repo config) on touched files; Catch2 v3, one
  `TEST_CASE` per behaviour; `ctest` green.
- Do not change SPEC-v1.md. If you think §5.2 should state the ordering
  guarantee, say so in your report.

## Task 1 — hold-and-replay in `EngineProcessor` (code + tests, TDD)

Files: `src/engine/EngineProcessor.h`, `src/engine/EngineProcessor.cpp`,
`tests/engine/EngineProcessorTests.cpp` (new `TEST_CASE`s; reuse the local
rig/builders in that file, or `tests/engine/EngineTestUtils.h`).

### Tests first (each must be red on the unchanged code, then green)

1. **`SwapGraph then FilePlay in one drain starts the file`** — processor with
   no graph (fresh `EngineProcessor`, `configureClock`, no
   `setGraphWhileStopped`). Build a graph with one file source (a synthetic
   reader as in `FilePlayerTests.cpp` / `TestReaders.h`; `DiskStreamer` not
   started, call `serviceAll()` before each block like the offline renderer).
   `swapGraph(graph)` then `push(cmd::FilePlay{1})`, then `process()` one or
   two blocks. Assert: a `fb::FileTransport` for source 1 with
   `playing == true` and the player's output is non-silent / position
   advancing; `droppedCommands() == 0`. This is the #36 reproduction.
2. **`Commands for a source only the new graph has survive a fade-out swap`** —
   old graph sources {1, 2}; new graph {1, 3} (3 = generator or file). Push
   `swapGraph(new)` then `cmd::SetSourceMute{3, true, 0}` (or `FilePlay{3}`)
   in the same drain. Block 1 fades 2 out and swaps after the block; after
   block 2 the mute (or play) is in effect on source 3 and
   `droppedCommands() == 0`. On unchanged code the command is applied to the
   old graph (no source 3) and lost.
3. **`A timed command due during a pending swap is applied after it`** —
   `pushAt(cmd::SetSourceMute{3, true, 0}, blockStartOfBlock1)` after a
   fade-out swap as in test 2; assert it lands on the new graph's source 3
   (block 2), not dropped.
4. **`A newer SwapGraph still supersedes a pending one while commands are
   held`** — swap A (fade-out case) then `SetSourceGain{3,…}` then swap B
   (also has source 3) in one drain; after the dust settles the live graph is
   B and the gain is applied; `droppedCommands() == 0`. (Guards the "never
   hold a SwapGraph" rule.)
5. **`setGraphWhileStopped drains everything queued before it`** — queue two
   `SwapGraph`s and a `SetSourceGain`, call `setGraphWhileStopped(deviceGraph)`,
   then `process()` a block: the live graph is still `deviceGraph` (compare a
   distinguishing property, e.g. sample rate or source set). If an equivalent
   test already exists, extend it rather than duplicate.

### Implementation (after the tests are red)

- Add a one-slot hold to the processor: `QueuedCommand held_{}; bool
  hasHeld_{false};` (fixed size, audio-thread only).
- `applyCommands(bool holdWhileSwapPending)` (or a member flag set by the
  caller): the audio thread passes true, `setGraphWhileStopped` passes false.
  Drain loop:
  - If `hasHeld_`: if `pending_ != nullptr` return (still waiting); else clear
    the slot and apply/insertTimed it first.
  - Pop `q`. Future-timed (`q.atSample > samplePosition_`) → `insertTimed`.
    Otherwise, if holding and `pending_ != nullptr` and `q.command` is not a
    `cmd::SwapGraph` → park it in `held_`, return (the rest stays in the FIFO,
    in order). Else `apply(q.command)` (a `SwapGraph` applies and supersedes).
- `applyTimedUpTo(sample)`: return without applying while `pending_ != nullptr`
  (sorted order is kept; they apply after the swap). `nextTimedOffset` may
  still split the block; that is harmless.
- `process()`: after `applyCommands()` and the pending-swap decision, if the
  swap was applied immediately (`pending_ == nullptr` afterwards), call
  `applyCommands()` and `applyTimedUpTo(blockStart)` once more so the held
  and queued commands act on the new graph in the same block. In the
  fade-out case they wait for the next block (the swap lands at the end of
  this one). Keep one swap per block: a second `SwapGraph` found in the
  second drain is held until the next block like today.
- Keep `applyRecall`'s "apply a pending swap first" line; it is now reached
  only when no swap is pending, which is fine.
- Update the header comment on `applyCommands`/`held_` (2–4 lines: the
  ordering invariant and why the hold exists; no issue numbers).

### Acceptance

- Tests 1–5 green; every pre-existing engine and core test green
  (`ctest --test-dir build`); allocation guard silent.
- Plant-the-defect check recorded in the report: revert the hold (keep the
  tests), confirm tests 1–3 go red on the assertion that names the lost
  command, restore, `cmp` the restore is byte-exact.
- `tools/EngineCheck.cpp` unchanged (it must now work as written).
- Report: what you verified vs inferred; whether SPEC §5.2 should state the
  guarantee; any behaviour change you noticed (e.g. command latency during an
  ambisonic crossfade wait, now up to the crossfade length).
