# 3D Stage View Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn the flat Top/Side stage into one rotatable 3D view that shows speaker height at a glance and draws each source as a coloured cloud linked to the speakers carrying it.

**Architecture:** `StageTransform` becomes an orthographic orbit camera (yaw + pitch) whose Top and Side snaps reproduce today's two views exactly, so hit-testing and dragging generalise without new cases. New JUCE-free helpers in `src/ui/logic` compute per-speaker shares (from the engine's own `Vbap`/`Dbap`/`Direct` classes), cloud dot counts, source colours and draw order. `StageView` only draws and routes the mouse, as today.

**Tech Stack:** C++20, JUCE 8.0.15 (`juce::Graphics`, no OpenGL), Catch2 v3.16.1, CMake + Ninja.

**Spec:** `docs/superpowers/specs/2026-11-10-stage-3d-view-design.md` (read it first; "Calls made by the designer" 1–17 are binding). Project contract: `docs/SPEC-v1.md` §2, §6, §7, §16; `CLAUDE.md`.

## Global Constraints

- Work only in the worktree `/Users/yishay/projects/spacer.worktrees/stage-3d-view`, branch `stage-3d-view`. Never commit to `main`. Never touch `/Users/yishay/projects/spacer`.
- Never `git stash`, `reset`, `checkout --` or `clean` the tree. You are the only builder; leave no background processes. One build at a time.
- `src/ui/logic` is plain C++20: no JUCE headers. It may include `core/*` and the JUCE-free `engine/Feedback.h` / `engine/Command.h` only. `src/core` is not modified by this plan.
- No change to the engine, commands, feedback, or the project file format.
- Coordinates are SPEC §2 only: x right, y front, z up, metres; azimuth 0 = front, +90 = right.
- Existing assertions keep their numbers. A test may be respelled to the new API; its expected values may not change. If one cannot pass unchanged, stop and report — do not edit the expectation.
- Tests: Catch2 v3, one `TEST_CASE` per behaviour, files `tests/ui/<Thing>Tests.cpp` (picked up by glob; re-run `cmake -S . -B build` after adding a file).
- Build is warning-free (`-Wall -Wextra -Wpedantic -Wshadow -Wconversion` on `spacer_ui_logic`); fix warnings, do not suppress them.
- Comments: default to none. No dates, issue numbers, plan/task ids or history in comments. A header comment is 2–4 lines.
- `clang-format` is not installed on this Mac and CI does not run it (checked 2026-11-10): match the surrounding style by hand (4 spaces, 110 columns, as in the neighbouring files). Do not install tools.
- Commit messages: short imperative subject, body says why, ending with the line `Co-Authored-By: Claude Fable 5.2 <noreply@anthropic.com>`.
- JUCE API facts are verified against the fetched source, `/Users/yishay/projects/spacer/build/_deps/juce-src/modules/`, not from memory.

### Build and test commands

```bash
cd /Users/yishay/projects/spacer.worktrees/stage-3d-view
D=/Users/yishay/projects/spacer/build/_deps
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DFETCHCONTENT_SOURCE_DIR_JUCE=$D/juce-src \
  -DFETCHCONTENT_SOURCE_DIR_CATCH2=$D/catch2-src \
  -DFETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON=$D/nlohmann_json-src   # reuses the downloaded deps
cmake --build build --target spacer_tests > build/last-build.log 2>&1; echo "build: $?"; grep -E 'warning|error|FAILED' build/last-build.log
build/tests/spacer_tests "[ui]"            # focused run; tags per task below
cmake --build build > build/last-build.log 2>&1; echo "build: $?"; grep -E 'warning|error|FAILED' build/last-build.log   # whole tree incl. the app
ctest --test-dir build --output-on-failure 2>&1 | tail -5   # the gate
```

The gate for every task is: whole-tree build prints no `warning`/`error` line and `build: 0`, and `ctest` reports `100% tests passed`.

## Review Focus

1. **Non-finite positions** (a NaN source position from feedback): the stage must skip the item, not hand NaN to `juce::Graphics`. Test in Task 2 (`farToNear` drops them).
2. **Zero-size viewport** (window minimised, component not laid out yet): every transform result stays finite. Test in Task 1.
3. **A source at the listener (distance 0) or exactly on a speaker:** shares are finite and the view draws. Test in Task 3.
4. **Speakers below the floor and viewing from underneath** (pitch −90): basis stays valid, order is still far-to-near. Tests in Task 1 (basis at −90) and Task 2 (negative z).
5. **A Direct source that lists no speakers, or ids not in the layout:** no lines, no crash. Test in Task 3.

## File Structure

| File | Responsibility | Task |
|------|----------------|------|
| `src/ui/logic/StageTransform.h/.cpp` (modify) | orbit camera, world ↔ screen | 1 |
| `src/ui/logic/StageHitTest.h` (modify, comment only) | depth wording | 1 |
| `src/ui/logic/StageDepth.h/.cpp` (create) | far-to-near order, depth cue | 2 |
| `src/ui/logic/StageShares.h/.cpp` (create) | per-speaker gains for a source point | 3 |
| `src/ui/logic/StageCloud.h/.cpp` (create) | dot table, dot counts, frame budget | 4 |
| `src/ui/logic/StagePalette.h/.cpp` (create) | colour per source id | 4 |
| `src/ui/logic/StageScene.h/.cpp` (modify) | source colour, level, shares | 5 |
| `src/ui/logic/PaintStats.h/.cpp` (create) | rolling paint time | 6 |
| `src/ui/StageView.h/.cpp` (modify) | drawing and mouse | 1 (compile only), 2, 6 |
| `docs/SPEC-v1.md`, `docs/DESIGN.md`, `docs/PLAN.md`, `docs/ui-checklist.md`, `docs/mac-validation/WP15.md` | docs | 7 |

Order: 1 → 2 → (3, 4 in either order) → 5 → 6 → 7 → 8. After Task 2 the 3D view is usable for speaker placement on its own. Model class: Tasks 1–7 sonnet; Task 8 opus.

---

### Task 1: Orbit camera in `StageTransform`

**Files:**
- Modify: `src/ui/logic/StageTransform.h`, `src/ui/logic/StageTransform.cpp`
- Modify: `src/ui/logic/StageHitTest.h` (the doc comment on `hitTest` only)
- Modify: `src/ui/StageView.h`, `src/ui/StageView.cpp` (compile-only respelling, see Step 5)
- Test: `tests/ui/StageTransformTests.cpp`, `tests/ui/StageHitTestTests.cpp`

**Interfaces:**
- Consumes: nothing new.
- Produces (in `StageTransform.h`, namespace `spacer`):

```cpp
struct StageCamera {
    float yawDeg{0.0f};     // (-180, 180]
    float pitchDeg{90.0f};  // [-90, 90]
    bool operator==(const StageCamera&) const = default;
};
enum class StageSnap { ThreeD, Top, Side, Front };

class StageTransform {
public:
    static constexpr float kOrbitDegPerPixel = 0.4f;
    /// ThreeD {-32, 24}, Top {0, 90}, Side {90, 0}, Front {0, 0}.
    static StageCamera cameraFor(StageSnap snap);

    /// Wraps yaw into (-180, 180], clamps pitch to [-90, 90]; ignored if either is not finite.
    void setCamera(const StageCamera& camera);
    const StageCamera& camera() const;
    void setSnap(StageSnap snap);              // setCamera(cameraFor(snap))
    std::optional<StageSnap> snap() const;     // the snap the camera equals exactly, if any
    /// yaw -= kOrbitDegPerPixel * dx; pitch += kOrbitDegPerPixel * dy.
    void orbit(float dxPixels, float dyPixels);

    Cartesian screenRight() const;   // r
    Cartesian screenUp() const;      // u
    Cartesian towardViewer() const;  // d
    // Unchanged signatures, now camera-relative: toPlane, depthOf, toScreen,
    // toWorld, screenDeltaToWorld, zoomAbout, setPan/pan, setExtent, setZoom, ...
};
```

`StageProjection`, `setView()` and `view()` are deleted. A default-constructed transform is at the Top snap. The basis is the one in the spec's "Camera" section; components with magnitude below 1e-6 are stored as 0. `toPlane(c) = {c·r, c·u}`, `depthOf(c) = c·d`, `toWorld(screen, depthFrom) = r·h + u·v + d·depthOf(depthFrom)`, `screenDeltaToWorld(dx, dy) = r·(dx/s) + u·(−dy/s)`. `fitExtent` becomes `max(kMinExtentMetres, farthest speaker distance from the origin × margin)`.

- [ ] **Step 1: Respell the existing tests, values untouched**

In `tests/ui/StageTransformTests.cpp` change the helper to `StageTransform square(StageSnap view = StageSnap::Top)` calling `t.setSnap(view)`, and replace every `StageProjection::Top` / `StageProjection::Side` with `StageSnap::Top` / `StageSnap::Side`. No expected value changes. `tests/ui/StageHitTestTests.cpp` needs no respelling (its `top()` helper relies on the default).

- [ ] **Step 2: Add the failing tests**

Append to `tests/ui/StageTransformTests.cpp` (add `#include <cmath>` and `#include <limits>`):

```cpp
TEST_CASE("Snap cameras give exact axis-aligned bases", "[ui][stage]") {
    StageTransform t;
    REQUIRE(t.snap() == StageSnap::Top);  // the default
    REQUIRE(t.screenRight() == Cartesian{1, 0, 0});
    REQUIRE(t.screenUp() == Cartesian{0, 1, 0});
    REQUIRE(t.towardViewer() == Cartesian{0, 0, 1});
    t.setSnap(StageSnap::Side);
    REQUIRE(t.screenRight() == Cartesian{0, 1, 0});
    REQUIRE(t.screenUp() == Cartesian{0, 0, 1});
    REQUIRE(t.towardViewer() == Cartesian{1, 0, 0});
    t.setSnap(StageSnap::Front);
    REQUIRE(t.screenRight() == Cartesian{1, 0, 0});
    REQUIRE(t.screenUp() == Cartesian{0, 0, 1});
    REQUIRE(t.towardViewer() == Cartesian{0, -1, 0});
    REQUIRE(StageTransform::cameraFor(StageSnap::ThreeD) == StageCamera{-32.0f, 24.0f});
}

TEST_CASE("Every camera has an orthonormal basis with a level floor", "[ui][stage]") {
    StageTransform t;
    for (float yaw : {-170.0f, -32.0f, 0.0f, 45.0f, 120.0f})
        for (float pitch : {-90.0f, -40.0f, 0.0f, 24.0f, 90.0f}) {
            t.setCamera({yaw, pitch});
            const Cartesian r = t.screenRight(), u = t.screenUp(), d = t.towardViewer();
            REQUIRE_THAT(r.length(), WithinAbs(1.0, 1e-5));
            REQUIRE_THAT(u.length(), WithinAbs(1.0, 1e-5));
            REQUIRE_THAT(d.length(), WithinAbs(1.0, 1e-5));
            REQUIRE_THAT(r.dot(u), WithinAbs(0.0, 1e-5));
            REQUIRE_THAT(r.dot(d), WithinAbs(0.0, 1e-5));
            REQUIRE_THAT(u.dot(d), WithinAbs(0.0, 1e-5));
            REQUIRE_THAT(r.cross(u).dot(d), WithinAbs(1.0, 1e-5));
            REQUIRE(r.z == 0.0f);   // the view never rolls
            REQUIRE(u.z >= 0.0f);   // world up never draws downwards
        }
}

TEST_CASE("Screen and world round-trip at a free camera angle", "[ui][stage]") {
    auto t = square();
    t.setViewport({30, 40, 640, 380});
    t.setZoom(1.7f);
    t.setPan({0.4f, -0.3f});
    t.setCamera({-32.0f, 24.0f});
    const Cartesian p{1.25f, -0.75f, 0.5f};
    const Cartesian back = t.toWorld(t.toScreen(p), p);
    REQUIRE_THAT(back.x, WithinAbs(p.x, 1e-4));
    REQUIRE_THAT(back.y, WithinAbs(p.y, 1e-4));
    REQUIRE_THAT(back.z, WithinAbs(p.z, 1e-4));
    // A drag moves the point in the screen plane and follows the mouse.
    const Cartesian moved = p + t.screenDeltaToWorld(37.0f, -12.0f);
    REQUIRE_THAT(t.depthOf(moved), WithinAbs(t.depthOf(p), 1e-4));
    REQUIRE_THAT(t.toScreen(moved).x - t.toScreen(p).x, WithinAbs(37.0, 1e-2));
    REQUIRE_THAT(t.toScreen(moved).y - t.toScreen(p).y, WithinAbs(-12.0, 1e-2));
}

TEST_CASE("Orbit turns 0.4 degrees per pixel, wraps yaw and clamps pitch", "[ui][stage]") {
    StageTransform t;
    t.setCamera({0.0f, 0.0f});
    t.orbit(10.0f, 25.0f);
    REQUIRE_THAT(t.camera().yawDeg, WithinAbs(-4.0, 1e-4));
    REQUIRE_THAT(t.camera().pitchDeg, WithinAbs(10.0, 1e-4));
    REQUIRE_FALSE(t.snap());
    t.orbit(0.0f, 1000.0f);
    REQUIRE(t.camera().pitchDeg == 90.0f);
    t.orbit(0.0f, -1000.0f);
    REQUIRE(t.camera().pitchDeg == -90.0f);
    t.setCamera({170.0f, 0.0f});
    t.orbit(-50.0f, 0.0f);  // 170 + 20 wraps to -170
    REQUIRE_THAT(t.camera().yawDeg, WithinAbs(-170.0, 1e-3));
    t.setSnap(StageSnap::Side);
    t.setCamera({std::numeric_limits<float>::quiet_NaN(), 0.0f});
    REQUIRE(t.snap() == StageSnap::Side);  // ignored
}

TEST_CASE("Rotating keeps the listener where it is on screen", "[ui][stage]") {
    auto t = square();
    t.zoomAbout({320, 90}, 2.5f);
    const auto before = t.toScreen({0, 0, 0});
    t.orbit(123.0f, -45.0f);
    const auto after = t.toScreen({0, 0, 0});
    REQUIRE_THAT(after.x, WithinAbs(before.x, 1e-3));
    REQUIRE_THAT(after.y, WithinAbs(before.y, 1e-3));
}

TEST_CASE("Fit extent is the farthest speaker distance", "[ui][stage]") {
    Layout box;
    box.speakers = {Speaker{1, "a", Position::fromCartesian({2, 2, 2})},
                    Speaker{2, "b", Position::fromCartesian({-2, -2, 0})}};
    REQUIRE_THAT(StageTransform::fitExtent(box), WithinAbs(std::sqrt(12.0) * 1.15, 1e-3));
}

TEST_CASE("A zero-size viewport still gives finite results", "[ui][stage]") {
    StageTransform t;
    t.setViewport({0, 0, 0, 0});
    t.setCamera({-32.0f, 24.0f});
    const auto s = t.toScreen({1, 2, 3});
    REQUIRE(std::isfinite(s.x));
    REQUIRE(std::isfinite(s.y));
    const Cartesian w = t.toWorld({5, 5}, {1, 2, 3});
    REQUIRE((std::isfinite(w.x) && std::isfinite(w.y) && std::isfinite(w.z)));
}
```

Append to `tests/ui/StageHitTestTests.cpp`:

```cpp
TEST_CASE("Hit test follows the camera", "[ui][hit]") {
    // Two speakers on one vertical line: one spot from above, apart from the front.
    const std::vector<StageItem> items = {
        {Kind::Speaker, 1, 0, {1.0f, 0.0f, 0.0f}},
        {Kind::Speaker, 2, 0, {1.0f, 0.0f, 1.0f}},
    };
    auto t = top();
    REQUIRE(hitTest(items, t, {300, 200})->id == 2);  // the higher one is nearer the viewer
    t.setSnap(StageSnap::Front);
    REQUIRE(hitTest(items, t, {300, 200})->id == 1);
    REQUIRE(hitTest(items, t, {300, 100})->id == 2);
}
```

- [ ] **Step 3: Run to see them fail**

Run: `cmake --build build --target spacer_tests 2>&1 | grep -E 'error' | head -5`
Expected: compile errors naming `StageSnap` / `setSnap` (not yet declared).

- [ ] **Step 4: Implement the camera in `StageTransform.h/.cpp`**

Signatures and formulas as in Interfaces. Cache `r`, `u`, `d` when the camera is set. Use `Position::wrapAzimuth` for yaw. Update the file's header comment to describe the camera (replace the Top/Side table) in ≤ 12 lines.

- [ ] **Step 5: Respell `StageView` so the app still compiles (behaviour unchanged)**

In `src/ui/StageView.h/.cpp`: `setProjection(StageProjection)` → `setSnap(StageSnap)`; `projection()` → `std::optional<StageSnap> snap() const`; the existing toggle button still switches between `StageSnap::Top` and `StageSnap::Side`; every `transform_.view() == StageProjection::Top` becomes `transform_.snap() == StageSnap::Top`. Nothing else; Task 2 rebuilds this view.

- [ ] **Step 6: In `StageHitTest.h`, reword the depth sentence**

Replace "(larger depth: higher z in the top view, larger x in the side view)" with "(larger `StageTransform::depthOf`)".

- [ ] **Step 7: Run the tests**

Run: `cmake --build build --target spacer_tests 2>&1 | grep -E 'warning|error'; build/tests/spacer_tests "[ui][stage],[ui][hit]"`
Expected: no warning/error lines; `All tests passed`.

- [ ] **Step 8: Run the gate, then commit**

```bash
git add src/ui/logic/StageTransform.h src/ui/logic/StageTransform.cpp src/ui/logic/StageHitTest.h \
        src/ui/StageView.h src/ui/StageView.cpp tests/ui/StageTransformTests.cpp tests/ui/StageHitTestTests.cpp
git commit -m "Make the stage transform an orbit camera"
```

---

### Task 2: 3D layout view (rotate, snaps, floor, stems, depth order)

**Files:**
- Create: `src/ui/logic/StageDepth.h`, `src/ui/logic/StageDepth.cpp`
- Modify: `src/ui/StageView.h`, `src/ui/StageView.cpp`
- Test: `tests/ui/StageDepthTests.cpp`

**Interfaces:**
- Consumes: Task 1's `StageTransform` (`setSnap`, `snap`, `orbit`, `depthOf`, `toScreen`, `extent`, basis accessors).
- Produces (in `StageDepth.h`):

```cpp
inline constexpr float kFarScale = 0.75f;
/// Indices into `positions`, farthest from the viewer first. Equal depths keep
/// their input order. Positions with a non-finite coordinate are left out.
std::vector<std::size_t> farToNear(std::span<const Cartesian> positions, const StageTransform& transform);
/// Size/brightness factor: 1 at depth +extent, kFarScale at depth -extent, linear between, clamped.
float depthCue(const Cartesian& c, const StageTransform& transform);
```

- Produces (in `StageView`): `void setSnap(StageSnap)`, `std::optional<StageSnap> snap() const`. The view opens at `StageSnap::ThreeD` in both modes.

- [ ] **Step 1: Write the failing tests** (`tests/ui/StageDepthTests.cpp`)

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <limits>
#include <vector>

#include "ui/logic/StageDepth.h"

using namespace spacer;
using Catch::Matchers::WithinAbs;

TEST_CASE("Far-to-near order follows the camera and is stable", "[ui][depth]") {
    const std::vector<Cartesian> pts = {{0, 0, 1}, {0, 0, 0}, {0, 0, 2}, {1, 0, 1}, {0, 0, -1}};
    StageTransform t;  // Top: depth is z
    REQUIRE(farToNear(pts, t) == std::vector<std::size_t>{4, 1, 0, 3, 2});
    t.setSnap(StageSnap::Side);  // depth is x
    REQUIRE(farToNear(pts, t) == std::vector<std::size_t>{0, 1, 2, 4, 3});
    t.setCamera({0.0f, -90.0f});  // from underneath: depth is -z
    REQUIRE(farToNear(pts, t) == std::vector<std::size_t>{2, 0, 3, 1, 4});
}

TEST_CASE("Non-finite positions are not drawn", "[ui][depth]") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    const std::vector<Cartesian> pts = {{0, 0, 1}, {nan, 0, 0}, {0, inf, 0}, {0, 0, 0}};
    REQUIRE(farToNear(pts, StageTransform{}) == std::vector<std::size_t>{3, 0});
}

TEST_CASE("Far items are a little smaller", "[ui][depth]") {
    StageTransform t;
    t.setExtent(2.0f);
    REQUIRE_THAT(depthCue({0, 0, 2}, t), WithinAbs(1.0, 1e-6));
    REQUIRE_THAT(depthCue({0, 0, 0}, t), WithinAbs(0.875, 1e-6));
    REQUIRE_THAT(depthCue({0, 0, -2}, t), WithinAbs(kFarScale, 1e-6));
    REQUIRE_THAT(depthCue({0, 0, 50}, t), WithinAbs(1.0, 1e-6));
    REQUIRE_THAT(depthCue({0, 0, -50}, t), WithinAbs(kFarScale, 1e-6));
}
```

- [ ] **Step 2: Run to see them fail**

Run: `cmake -S . -B build >/dev/null && cmake --build build --target spacer_tests 2>&1 | grep -E 'error' | head -3`
Expected: `StageDepth.h` file not found.

- [ ] **Step 3: Implement `StageDepth.h/.cpp`**, run `build/tests/spacer_tests "[ui][depth]"`, expect `All tests passed`.

- [ ] **Step 4: Rebuild `StageView` around the camera**

Requirements (the builder chooses colours, sizes and helper functions):

1. **Snap buttons.** Replace `viewButton_` with four `juce::TextButton`s, top-right, labelled `3D`, `Top`, `Side`, `Front`, each calling `setSnap`. A button shows as on only while `transform_.snap()` equals its snap. Tooltips: "Free 3D view", "From above: front is up", "From the right: front is right", "From behind the listener: right is right". Constructor calls `setSnap(StageSnap::ThreeD)`.
2. **Rotate.** `mouseDown` on empty space starts an orbit drag; `mouseDrag` calls `transform_.orbit` with the movement since the previous drag event, refreshes the button states and repaints. No undo gesture, no project edit. Item drags, clicks, wheel zoom and double-click refit work as before (refit leaves the camera angle alone).
3. **Floor** (replaces both branches of `paintGrid`): 1 m rings on z = 0 drawn as projected polylines (72 segments), out to at least `ceil(extent × 1.5)` metres, at most 50 rings; the two floor axes; the text `FRONT` just beyond the outer ring on +y; ring labels `1 m`, `2 m`, … along +x.
4. **Height mast:** a line from the origin up to `ceil(extent)` metres with a tick and label every metre.
5. **Listener:** head circle at the origin with a nose towards the projected +y direction; no nose when that projected direction is shorter than 1 px.
6. **Stems:** every speaker with `|z| > 0.02` m gets a thin line to `(x, y, 0)` and a small hollow dot there.
7. **Draw order:** speakers are drawn in `farToNear` order (which also skips non-finite positions); box size, ring and label alpha are multiplied by `depthCue`. Hover, selection and speaker-test outlines stay.
8. **Gizmo:** bottom-right, three short arrows along `screenRight/Up`-projected world x, y, z, labelled `R`, `F`, `Up`.
9. **Caption** (bottom-left), by `transform_.snap()`:
   - none or `ThreeD`: `3D view  -  drag empty space to rotate`
   - `Top`: `Top view  -  front is up`
   - `Side`: `Side view (from the right)  -  front is right, up is up`
   - `Front`: `Front view (from behind the listener)  -  right is right, up is up`
10. Sources, trajectories and the stereo-pair line keep their current look in this task (they go through `toScreen`, so they already rotate).

Update the class comment in `StageView.h` (≤ 6 lines): it is a 3D orbit view with snaps.

- [ ] **Step 5: Build the app and run the gate**

Run the whole-tree build and `ctest` from "Build and test commands". Expected: no warning/error lines, `build: 0`, `100% tests passed`.

- [ ] **Step 6: Smoke-launch**

Run: `open build/Spacer_artefacts/Release/Spacer.app` , confirm with `pgrep -x Spacer` that it is running, then quit it with `osascript -e 'quit app "Spacer"'` and confirm `pgrep -x Spacer` prints nothing. Report honestly that this proves launch only; the look is checked by the owner in Task 8.

- [ ] **Step 7: Commit**

```bash
git add src/ui/logic/StageDepth.h src/ui/logic/StageDepth.cpp tests/ui/StageDepthTests.cpp src/ui/StageView.h src/ui/StageView.cpp
git commit -m "Draw the stage as a rotatable 3D view"
```

---

### Task 3: `StageShares` — real per-speaker gains for a source point

**Files:**
- Create: `src/ui/logic/StageShares.h`, `src/ui/logic/StageShares.cpp`
- Test: `tests/ui/StageSharesTests.cpp`

**Interfaces:**
- Consumes: `core/spatial/Vbap.h`, `Dbap.h`, `Direct.h` (`prepare(layout, sampleRate, maxBlockSize)`, `gainsFor(pos, spread, out)`), `core/Source.h`.
- Produces:

```cpp
class StageShares {
public:
    /// Non-RT. Builds the panners for `layout`; call again when the layout changes.
    void prepare(const Layout& layout);
    int numSpeakers() const;  // 0 before prepare()
    /// Linear gain per layout speaker index for one point of `source` at
    /// `position`, using `source.spatializer`, `source.spread` and
    /// `source.directSpeakers`. Returns false and zero-fills `out` when there
    /// are no per-speaker gains: Ambisonic source, not prepared, or
    /// `out.size() != numSpeakers()`.
    bool gainsFor(const SourceSpec& source, const Position& position, std::span<float> out) const;
};
```

Use the core classes themselves — never re-derive panning. Prepare with sample rate 48000.0 and block size 64. First read `Vbap.cpp` / `Dbap.cpp` to confirm `gainsFor` does not depend on those two values; if it does, stop and report.

- [ ] **Step 1: Write the failing tests** (`tests/ui/StageSharesTests.cpp`)

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <vector>

#include "core/LayoutPresets.h"
#include "core/spatial/Dbap.h"
#include "core/spatial/Vbap.h"
#include "ui/logic/StageShares.h"

using namespace spacer;
using Catch::Matchers::WithinAbs;

namespace {
std::size_t lfeIndex(const Layout& l) {
    for (std::size_t i = 0; i < l.speakers.size(); ++i)
        if (l.speakers[i].lfe) return i;
    FAIL("layout has no LFE");
    return 0;
}
}  // namespace

TEST_CASE("Shares equal the engine panners' gains", "[ui][shares]") {
    const Layout dome = *layoutpresets::load("dome22");
    StageShares shares;
    shares.prepare(dome);
    REQUIRE(shares.numSpeakers() == 22);
    const Position pos{37.0f, 22.0f, 1.5f};
    SourceSpec s;
    s.spread = 0.3f;
    std::vector<float> got(22), want(22);

    s.spatializer = SpatializerKind::Vbap;
    Vbap vbap;
    vbap.prepare(dome, 48000.0, 64);
    vbap.gainsFor(pos, 0.3f, want);
    REQUIRE(shares.gainsFor(s, pos, got));
    REQUIRE(got == want);

    s.spatializer = SpatializerKind::Dbap;
    Dbap dbap;
    dbap.prepare(dome, 48000.0, 64);
    dbap.gainsFor(pos, 0.3f, want);
    REQUIRE(shares.gainsFor(s, pos, got));
    REQUIRE(got == want);
}

TEST_CASE("Direct shares are the listed speakers, LFE included", "[ui][shares]") {
    const Layout l = *layoutpresets::load("5.1");
    const std::size_t lfe = lfeIndex(l);
    StageShares shares;
    shares.prepare(l);
    SourceSpec s;
    s.spatializer = SpatializerKind::Direct;
    s.directSpeakers = {l.speakers[0].id, l.speakers[lfe].id, 999};  // 999 is not in the layout
    std::vector<float> got(l.speakers.size(), 7.0f);
    REQUIRE(shares.gainsFor(s, {}, got));
    for (std::size_t i = 0; i < got.size(); ++i) REQUIRE(got[i] == ((i == 0 || i == lfe) ? 1.0f : 0.0f));
    s.directSpeakers.clear();
    REQUIRE(shares.gainsFor(s, {}, got));
    for (float g : got) REQUIRE(g == 0.0f);
}

TEST_CASE("VBAP and DBAP never share to the LFE speaker", "[ui][shares]") {
    const Layout l = *layoutpresets::load("5.1");
    StageShares shares;
    shares.prepare(l);
    std::vector<float> got(l.speakers.size());
    for (auto kind : {SpatializerKind::Vbap, SpatializerKind::Dbap}) {
        SourceSpec s;
        s.spatializer = kind;
        REQUIRE(shares.gainsFor(s, {0.0f, 0.0f, 2.0f}, got));
        REQUIRE(got[lfeIndex(l)] == 0.0f);
    }
}

TEST_CASE("Ambisonic sources and unprepared shares report no gains", "[ui][shares]") {
    StageShares shares;
    std::vector<float> out(4, 7.0f);
    SourceSpec s;
    REQUIRE(shares.numSpeakers() == 0);
    REQUIRE_FALSE(shares.gainsFor(s, {}, out));  // not prepared
    for (float g : out) REQUIRE(g == 0.0f);
    shares.prepare(*layoutpresets::load("quad"));
    s.spatializer = SpatializerKind::Ambisonic;
    out.assign(4, 7.0f);
    REQUIRE_FALSE(shares.gainsFor(s, {}, out));
    for (float g : out) REQUIRE(g == 0.0f);
    std::vector<float> wrong(3, 7.0f);
    s.spatializer = SpatializerKind::Vbap;
    REQUIRE_FALSE(shares.gainsFor(s, {}, wrong));  // size mismatch
}

TEST_CASE("Preparing again follows the new layout", "[ui][shares]") {
    StageShares shares;
    shares.prepare(*layoutpresets::load("quad"));
    REQUIRE(shares.numSpeakers() == 4);
    shares.prepare(*layoutpresets::load("dome22"));
    REQUIRE(shares.numSpeakers() == 22);
    std::vector<float> got(22);
    REQUIRE(shares.gainsFor(SourceSpec{}, {10.0f, 40.0f, 2.0f}, got));
    double sumSq = 0.0;
    for (float g : got) sumSq += static_cast<double>(g) * g;
    REQUIRE_THAT(sumSq, WithinAbs(1.0, 1e-3));
}

TEST_CASE("Shares are finite at the listener and on a speaker", "[ui][shares]") {
    const Layout dome = *layoutpresets::load("dome22");
    StageShares shares;
    shares.prepare(dome);
    std::vector<float> got(22);
    for (auto kind : {SpatializerKind::Vbap, SpatializerKind::Dbap})
        for (const Position& pos : {Position{0.0f, 0.0f, 0.0f}, dome.speakers[9].position}) {
            SourceSpec s;
            s.spatializer = kind;
            REQUIRE(shares.gainsFor(s, pos, got));
            for (float g : got) REQUIRE(std::isfinite(g));
        }
}
```

- [ ] **Step 2: Run to see them fail**

Run: `cmake -S . -B build >/dev/null && cmake --build build --target spacer_tests 2>&1 | grep -E 'error' | head -3`
Expected: `StageShares.h` file not found.

- [ ] **Step 3: Implement `StageShares`.** If "Shares are finite at the listener and on a speaker" fails because a core panner returns a non-finite gain, do not patch `src/core`: stop and report the position and panner.

- [ ] **Step 4: Run** `build/tests/spacer_tests "[ui][shares]"` — expect `All tests passed`. Then the gate.

- [ ] **Step 5: Commit**

```bash
git add src/ui/logic/StageShares.h src/ui/logic/StageShares.cpp tests/ui/StageSharesTests.cpp
git commit -m "Compute per-speaker shares for the stage from the core panners"
```

---

### Task 4: `StageCloud` and `StagePalette`

**Files:**
- Create: `src/ui/logic/StageCloud.h`, `src/ui/logic/StageCloud.cpp`, `src/ui/logic/StagePalette.h`, `src/ui/logic/StagePalette.cpp`
- Test: `tests/ui/StageCloudTests.cpp`, `tests/ui/StagePaletteTests.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces (`StageCloud.h`):

```cpp
struct CloudOffset {
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};
    float phase{0.0f};  // radians, [0, 2π)
};
inline constexpr int kCloudMaxDots = 96;
inline constexpr int kCloudMinBudgetDots = 24;
inline constexpr int kCloudFrameBudget = 6000;

/// The one fixed table of kCloudMaxDots unit-variance offsets; identical on every platform.
std::span<const CloudOffset> cloudOffsets();
/// Most dots one cloud may use when `loudPoints` clouds are drawn:
/// clamp(kCloudFrameBudget / loudPoints, kCloudMinBudgetDots, kCloudMaxDots); kCloudMaxDots when loudPoints <= 0.
int cloudDotLimit(int loudPoints);
/// round(fraction × limit) with fraction clamped to [0, 1]; at least 1 when
/// fraction > 0 and limit > 0; 0 when fraction is not finite or limit <= 0.
int cloudDots(float fraction, int limit);
```

The table generator is fixed so the picture is the same everywhere (no `<random>` distributions): state `std::uint32_t s = 0x53504143u`; `next() { s = s * 1664525u + 1013904223u; return static_cast<float>(s >> 8) / 16777216.0f; }`; one coordinate is `(next() + next() + next() + next() - 2.0f) * std::sqrt(3.0f)` accumulated left to right in `float`; each entry draws x, then y, then z, then `phase = next() * 2π`.

- Produces (`StagePalette.h`):

```cpp
struct StageRgb {
    std::uint8_t r{0};
    std::uint8_t g{0};
    std::uint8_t b{0};
    bool operator==(const StageRgb&) const = default;
};
/// Hue in [0, 1): the fractional part of sourceId × 0.61803399 (golden-ratio steps).
float sourceHue(int sourceId);
/// HSV(sourceHue(id), 0.65, 1.0) as 8-bit RGB.
StageRgb sourceColour(int sourceId);
```

- [ ] **Step 1: Write the failing tests**

`tests/ui/StageCloudTests.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <limits>

#include "ui/logic/StageCloud.h"

using namespace spacer;
using Catch::Matchers::WithinAbs;

TEST_CASE("Cloud offsets are one fixed, centred, unit-sized table", "[ui][cloud]") {
    const auto o = cloudOffsets();
    REQUIRE(o.size() == static_cast<std::size_t>(kCloudMaxDots));
    REQUIRE(o.data() == cloudOffsets().data());
    REQUIRE_THAT(o[0].x, WithinAbs(0.978417, 1e-4));
    REQUIRE_THAT(o[0].y, WithinAbs(2.024173, 1e-4));
    REQUIRE_THAT(o[0].z, WithinAbs(-1.712411, 1e-4));
    REQUIRE_THAT(o[0].phase, WithinAbs(1.936542, 1e-4));
    REQUIRE_THAT(o[95].x, WithinAbs(0.880879, 1e-4));
    REQUIRE_THAT(o[95].phase, WithinAbs(3.399481, 1e-4));
    double mx = 0, my = 0, mz = 0, sq = 0;
    for (const auto& p : o) {
        mx += p.x; my += p.y; mz += p.z;
        sq += p.x * p.x + p.y * p.y + p.z * p.z;
        REQUIRE(std::abs(p.x) <= 3.47f);
        REQUIRE(std::abs(p.y) <= 3.47f);
        REQUIRE(std::abs(p.z) <= 3.47f);
        REQUIRE(p.phase >= 0.0f);
        REQUIRE(p.phase < 6.2832f);
    }
    REQUIRE_THAT(mx / 96.0, WithinAbs(0.041, 0.01));
    REQUIRE_THAT(my / 96.0, WithinAbs(0.085, 0.01));
    REQUIRE_THAT(mz / 96.0, WithinAbs(-0.074, 0.01));
    REQUIRE_THAT(sq / (3.0 * 96.0), WithinAbs(0.897, 0.01));
}

TEST_CASE("The dot limit shares the frame budget", "[ui][cloud]") {
    REQUIRE(cloudDotLimit(-3) == 96);
    REQUIRE(cloudDotLimit(0) == 96);
    REQUIRE(cloudDotLimit(1) == 96);
    REQUIRE(cloudDotLimit(62) == 96);
    REQUIRE(cloudDotLimit(63) == 95);
    REQUIRE(cloudDotLimit(128) == 46);  // 64 stereo sources, all loud
    REQUIRE(cloudDotLimit(250) == 24);
    REQUIRE(cloudDotLimit(100000) == 24);
}

TEST_CASE("Louder clouds have more dots", "[ui][cloud]") {
    REQUIRE(cloudDots(0.0f, 96) == 0);
    REQUIRE(cloudDots(1.0f, 96) == 96);
    REQUIRE(cloudDots(0.5f, 96) == 48);
    REQUIRE(cloudDots(0.001f, 96) == 1);
    REQUIRE(cloudDots(2.0f, 96) == 96);
    REQUIRE(cloudDots(-1.0f, 96) == 0);
    REQUIRE(cloudDots(std::numeric_limits<float>::quiet_NaN(), 96) == 0);
    REQUIRE(cloudDots(0.5f, 0) == 0);
    int previous = 0;
    for (int i = 0; i <= 20; ++i) {
        const int n = cloudDots(static_cast<float>(i) * 0.05f, 46);
        REQUIRE(n >= previous);
        previous = n;
    }
}
```

`tests/ui/StagePaletteTests.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>

#include "ui/logic/StagePalette.h"

using namespace spacer;

namespace {
float hueGap(float a, float b) {
    const float d = std::abs(a - b);
    return std::min(d, 1.0f - d);
}
}  // namespace

TEST_CASE("Every source id has its own stable colour", "[ui][palette]") {
    REQUIRE(sourceColour(7) == sourceColour(7));
    for (int a = 1; a <= 64; ++a) {
        REQUIRE(sourceHue(a) >= 0.0f);
        REQUIRE(sourceHue(a) < 1.0f);
        for (int b = a + 1; b <= 64; ++b) {
            REQUIRE(hueGap(sourceHue(a), sourceHue(b)) > 0.005f);
            REQUIRE_FALSE(sourceColour(a) == sourceColour(b));
        }
    }
    for (int a = 1; a < 64; ++a) REQUIRE(hueGap(sourceHue(a), sourceHue(a + 1)) > 0.3f);  // neighbours contrast
    for (int id : {0, -5}) {
        REQUIRE(sourceHue(id) >= 0.0f);
        REQUIRE(sourceHue(id) < 1.0f);
    }
}

TEST_CASE("Source colours are bright and saturated", "[ui][palette]") {
    for (int id = 1; id <= 64; ++id) {
        const StageRgb c = sourceColour(id);
        REQUIRE(std::max({c.r, c.g, c.b}) == 255);                  // value 1.0
        REQUIRE(std::min({c.r, c.g, c.b}) == 89);                   // 255 × (1 − 0.65), rounded
    }
}
```

- [ ] **Step 2: Run to see them fail** (headers not found), as in Task 3 Step 2.

- [ ] **Step 3: Implement both files.** If a pinned table value in the first test is off by more than 1e-4, the generator differs from the one specified above: fix the generator, not the test.

- [ ] **Step 4: Run** `build/tests/spacer_tests "[ui][cloud],[ui][palette]"` — expect `All tests passed`. Then the gate.

- [ ] **Step 5: Commit**

```bash
git add src/ui/logic/StageCloud.h src/ui/logic/StageCloud.cpp src/ui/logic/StagePalette.h src/ui/logic/StagePalette.cpp \
        tests/ui/StageCloudTests.cpp tests/ui/StagePaletteTests.cpp
git commit -m "Add the stage cloud table, dot budget and source palette"
```

---

### Task 5: `StageScene` carries colour, level and shares

**Files:**
- Modify: `src/ui/logic/StageScene.h`, `src/ui/logic/StageScene.cpp`
- Test: `tests/ui/StageSceneTests.cpp`

**Interfaces:**
- Consumes: `StageShares` (Task 3), `sourceColour` / `StageRgb` (Task 4), `FeedbackState::sourceLevelDb`, `FeedbackState::meterFraction`, `kMaxSpeakers` (`core/Layout.h`).
- Produces (additions in `StageScene.h`):

```cpp
inline constexpr float kMinDrawnShare = 0.03f;

struct StageSourcePoint {
    // existing: id, channel, name, position, mute
    StageRgb colour;                            // sourceColour(id)
    float level{0.0f};                          // meterFraction(sourceLevelDb(id)); 0 when muted
    bool hasShares{false};                      // false: muted, silent, Ambisonic
    std::array<float, kMaxSpeakers> shares{};   // gain × level per layout speaker index; below kMinDrawnShare → 0
};

class StageScene {
    // existing API unchanged, plus:
    int loudPoints() const;   // source points with level > 0
};
```

`update()` re-prepares its `StageShares` whenever `project.layout` differs from the layout it last prepared for (so `update()` works without a prior `setProject()`, as the existing tests use it). Shares use the point's drawn position (`Position::fromCartesian` of the same value stored in `position`, or the `Position` before conversion) and the source's base `spread`. Both points of a stereo source use the source's single meter value.

- [ ] **Step 1: Write the failing tests** (append to `tests/ui/StageSceneTests.cpp`; add `#include <cmath>`)

```cpp
TEST_CASE("Source points carry colour, level and shares", "[ui][scene]") {
    const Project p = makeProject();  // quad; source 1 is Manual at az 0, 1 m: between FL and FR
    StageScene scene;
    FeedbackState f;
    scene.update(p, f);
    REQUIRE(scene.sources()[0].colour == sourceColour(1));
    REQUIRE(scene.sources()[1].colour == sourceColour(2));
    REQUIRE(scene.sources()[0].level == 0.0f);  // silent: a dot only
    REQUIRE_FALSE(scene.sources()[0].hasShares);
    REQUIRE(scene.loudPoints() == 0);

    f.apply(fb::SourceMeter{1, {1.0f, 0.0f}});  // 0 dBFS
    scene.update(p, f);
    const auto& s = scene.sources()[0];
    REQUIRE(s.level == 1.0f);
    REQUIRE(s.hasShares);
    REQUIRE(scene.loudPoints() == 1);
    for (std::size_t i = 0; i < 4; ++i) {
        const bool front = std::abs(p.layout.speakers[i].position.azimuth) < 90.0f;
        REQUIRE_THAT(s.shares[i], WithinAbs(front ? 0.70711 : 0.0, 1e-3));
    }
    for (std::size_t i = 4; i < s.shares.size(); ++i) REQUIRE(s.shares[i] == 0.0f);
}

TEST_CASE("Shares scale with level and tiny shares are dropped", "[ui][scene]") {
    const Project p = makeProject();
    StageScene scene;
    FeedbackState half;
    half.apply(fb::SourceMeter{1, {0.0316228f, 0.0f}});  // -30 dBFS: fraction 0.5
    scene.update(p, half);
    REQUIRE_THAT(scene.sources()[0].level, WithinAbs(0.5, 1e-3));
    REQUIRE_THAT(scene.sources()[0].shares[0], WithinAbs(0.35355, 1e-3));
    FeedbackState quiet;
    quiet.apply(fb::SourceMeter{1, {0.0013183f, 0.0f}});  // -57.6 dBFS: fraction 0.04, share 0.028
    scene.update(p, quiet);
    REQUIRE(scene.sources()[0].level > 0.0f);
    REQUIRE(scene.loudPoints() == 1);
    for (float share : scene.sources()[0].shares) REQUIRE(share == 0.0f);
}

TEST_CASE("Muted and ambisonic sources have no shares", "[ui][scene]") {
    Project p = makeProject();
    FeedbackState f;
    f.apply(fb::SourceMeter{1, {1.0f, 0.0f}});
    StageScene scene;
    p.sources[0].mute = true;
    scene.update(p, f);
    REQUIRE(scene.sources()[0].level == 0.0f);
    REQUIRE_FALSE(scene.sources()[0].hasShares);
    REQUIRE(scene.loudPoints() == 0);
    p.sources[0].mute = false;
    p.sources[0].spatializer = SpatializerKind::Ambisonic;
    scene.update(p, f);
    REQUIRE(scene.sources()[0].level == 1.0f);
    REQUIRE_FALSE(scene.sources()[0].hasShares);
    for (float share : scene.sources()[0].shares) REQUIRE(share == 0.0f);
}

TEST_CASE("Shares follow a layout change and stereo points share one level", "[ui][scene]") {
    Project p = makeProject();
    FeedbackState f;
    f.apply(fb::SourceMeter{1, {1.0f, 0.0f}});
    f.apply(fb::SourceMeter{2, {1.0f, 0.2f}});
    StageScene scene;
    scene.update(p, f);
    REQUIRE(scene.sources()[1].level == scene.sources()[2].level);
    REQUIRE(scene.loudPoints() == 3);
    p.layout = *layoutpresets::load("dome22");
    scene.update(p, f);
    double sumSq = 0.0;
    for (float share : scene.sources()[0].shares) sumSq += static_cast<double>(share) * share;
    REQUIRE(sumSq > 0.99);
    REQUIRE(sumSq < 1.001);
}
```

- [ ] **Step 2: Run to see them fail**

Run: `cmake --build build --target spacer_tests 2>&1 | grep -E 'error' | head -3`
Expected: `no member named 'colour'` (or `loudPoints`).

- [ ] **Step 3: Implement.** If an existing `StageSceneTests` expectation changes, stop and report.

- [ ] **Step 4: Run** `build/tests/spacer_tests "[ui][scene]"` — expect `All tests passed`. Then the gate.

- [ ] **Step 5: Commit**

```bash
git add src/ui/logic/StageScene.h src/ui/logic/StageScene.cpp tests/ui/StageSceneTests.cpp
git commit -m "Give stage source points a colour, a level and speaker shares"
```

---

### Task 6: Draw sources as clouds linked to their speakers

**Files:**
- Create: `src/ui/logic/PaintStats.h`, `src/ui/logic/PaintStats.cpp`
- Modify: `src/ui/StageView.h`, `src/ui/StageView.cpp`
- Test: `tests/ui/PaintStatsTests.cpp`

**Interfaces:**
- Consumes: `StageSourcePoint::{colour, level, hasShares, shares}`, `StageScene::loudPoints()` (Task 5); `cloudOffsets`, `cloudDotLimit`, `cloudDots` (Task 4); `farToNear`, `depthCue` (Task 2); `AppContext::kTickHz`.
- Produces (`PaintStats.h`):

```cpp
/// Mean and maximum of the last kWindow paint times.
class PaintStats {
public:
    static constexpr int kWindow = 60;
    void add(double milliseconds);
    double meanMs() const;  // 0 when empty
    double maxMs() const;   // 0 when empty
    int count() const;      // <= kWindow
};
```

- [ ] **Step 1: Write the failing test** (`tests/ui/PaintStatsTests.cpp`)

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "ui/logic/PaintStats.h"

using namespace spacer;
using Catch::Matchers::WithinAbs;

TEST_CASE("Paint stats cover the last 60 paints", "[ui][paintstats]") {
    PaintStats s;
    REQUIRE(s.count() == 0);
    REQUIRE(s.meanMs() == 0.0);
    REQUIRE(s.maxMs() == 0.0);
    for (int i = 1; i <= 60; ++i) s.add(i);
    REQUIRE(s.count() == 60);
    REQUIRE_THAT(s.meanMs(), WithinAbs(30.5, 1e-9));
    REQUIRE_THAT(s.maxMs(), WithinAbs(60.0, 1e-9));
    s.add(100.0);  // pushes out the 1
    REQUIRE(s.count() == 60);
    REQUIRE_THAT(s.meanMs(), WithinAbs(1929.0 / 60.0, 1e-9));
    REQUIRE_THAT(s.maxMs(), WithinAbs(100.0, 1e-9));
    for (int i = 0; i < 60; ++i) s.add(2.0);  // the 100 has left the window
    REQUIRE_THAT(s.maxMs(), WithinAbs(2.0, 1e-9));
}
```

- [ ] **Step 2: Run to see it fail, implement `PaintStats`, run** `build/tests/spacer_tests "[ui][paintstats]"` — expect `All tests passed`.

- [ ] **Step 3: Draw the sources in `StageView`** (Perform mode only; LayoutEdit still shows speakers only)

Requirements (the builder chooses sizes, alphas and the shimmer):

1. **Colour.** A source's dot, its trajectory (faint), its stereo-pair line and its label marker use `point.colour`. Muted sources stay `kSourceMuted` grey and draw no cloud, lines or glow. Selection and hover rings stay.
2. **Stem.** Every source point with `|z| > 0.02` m has a dashed stem to the floor and a small hollow floor dot, in its colour.
3. **Cloud.** For a point with `level > 0`: `cloudDots(level, cloudDotLimit(scene_.loudPoints()))` dots, the first N entries of `cloudOffsets()`, scaled to about 0.20 m around the point (world metres, so the cloud zooms with the view), each dot a 2–3 px square. Collect one cloud's dots in a `juce::RectangleList<float>` and draw it with one `g.fillRectList` (declared at `juce_graphics/contexts/juce_GraphicsContext.h:293`; verify there).
4. **Share lines.** For a point with `hasShares`: a thin line from the point to each speaker with `shares[i] > 0`, alpha rising with the share (cap about 0.8). Drawn before the depth-sorted items.
5. **Speaker glow.** On each such speaker, a smaller cloud (about 0.13 m) of `cloudDots(shares[i], cloudDotLimit(scene_.loudPoints()) / 4)` dots in the source's colour, drawn with that speaker.
6. **Order.** Speakers and source points go through one `farToNear` call together and are drawn in that order, each scaled by `depthCue`; non-finite items are thereby skipped.
7. **Shimmer.** Dots move slightly using `CloudOffset::phase` and an animation time that advances by `1.0 / AppContext::kTickHz` on each `feedbackUpdated()`. No wall clock.
8. **Paint time.** Time `paint()` with `juce::Time::getMillisecondCounterHiRes()` into a `PaintStats`. When `juce::SystemStats::getEnvironmentVariable("SPACER_STAGE_STATS", "") == "1"` (read once in the constructor), append `   paint 3.2 ms mean / 5.1 ms max` (one decimal) to the caption.

- [ ] **Step 4: Build the app and run the gate.** Expected: no warning/error lines, `build: 0`, `100% tests passed`.

- [ ] **Step 5: Smoke-launch** exactly as Task 2 Step 6. Then once more with the readout on: start `SPACER_STAGE_STATS=1 build/Spacer_artefacts/Release/Spacer.app/Contents/MacOS/Spacer` in the background, check with `kill -0 <pid>` that it is still alive a few seconds later, `kill <pid>`, wait for it, and confirm `pgrep -x Spacer` prints nothing.

- [ ] **Step 6: Commit**

```bash
git add src/ui/logic/PaintStats.h src/ui/logic/PaintStats.cpp tests/ui/PaintStatsTests.cpp src/ui/StageView.h src/ui/StageView.cpp
git commit -m "Draw sources as coloured clouds linked to their speakers"
```

---

### Task 7: Docs

**Files:**
- Modify: `docs/SPEC-v1.md` (§16 item 1), `docs/DESIGN.md` (three places), `docs/PLAN.md`, `docs/ui-checklist.md`
- Create: `docs/mac-validation/WP15.md`

- [ ] **Step 1: SPEC §16 item 1.** Replace the item (from `1. **Stage** (centre):` to the line ending `⌘Z undo.`) with exactly:

```markdown
1. **Stage** (centre): 3D view of layout and sources (orthographic, the floor
   always level). Drag empty space to rotate, wheel to zoom; snap buttons
   3D / Top / Side / Front. A stem to the floor shows the height of every
   speaker and source. Drag sources (sets Manual motion value or offsets the
   motion centre); a drag moves in the screen plane. Trajectories drawn as
   faint paths. Each source has its own colour and is drawn as a cloud whose
   density follows its level, with a line to, and a smaller cloud on, each
   speaker that carries it (VBAP, DBAP, Direct; ambisonic sources show the
   cloud only). Speaker meters as rings; click a speaker to solo pink noise.
   Keyboard: space = Go, Esc = panic, ⌘Z undo.
```

- [ ] **Step 2: DESIGN.md.** In the architecture box, `stage view (top/side)` → `stage view (3D)` keeping the box's right border aligned (pad with spaces). `Layouts are edited visually (top and side views) or numerically` → `Layouts are edited visually (3D view with top, side and front snaps) or numerically`. `**Stage view** — top-down (and side) view of the layout` → `**Stage view** — rotatable 3D view (with top, side and front snaps) of the layout`.

- [ ] **Step 3: PLAN.md.** After the WP14 section add:

```markdown
## WP15 — 3D stage view · **macOS · M**

Owner-requested change to §16.1: the stage becomes a rotatable 3D view and
sources are drawn as coloured clouds linked to the speakers carrying them.
Design: `docs/superpowers/specs/2026-11-10-stage-3d-view-design.md`. Plan:
`docs/superpowers/plans/2026-11-10-stage-3d-view.md`.
```

and this status-log row (the status becomes `done` only when the owner has run the Mac checks):

```markdown
| WP15 | built, Mac checks open | Orbit camera in `StageTransform` (Top/Side snaps identical to the old views), `StageDepth`, `StageShares` (core panners' `gainsFor`), `StageCloud`, `StagePalette`, `PaintStats`; `StageView` 3D drawing. Mac checks: `docs/mac-validation/WP15.md`. |
```

- [ ] **Step 4: `docs/mac-validation/WP15.md`**, in the format of `docs/mac-validation/WP14.md` (intro, Setup, numbered Checks with pass/fail and notes). Checks:
  1. Stage opens in 3D; dragging empty space rotates; the floor stays level; looking from underneath works.
  2. Top and Side snaps look like the previous Top and Side views (positions), Front shows right on the right.
  3. Dome 22: every speaker above the floor has a stem; heights of the three rings are readable.
  4. Layout editor: drag a speaker in Top, then in Side, then in free 3D; the table values follow; ⌘Z undoes each drag as one step.
  5. `resources/examples/stage-demo.spacer` playing: each source has its own colour; a louder source has a denser cloud; a silent one is a dot.
  6. An orbiting VBAP source: lines and speaker clouds hand over from speaker to speaker as it moves.
  7. A DBAP source, a Direct source (to two speakers incl. an LFE) and an Ambisonic source: many faint lines / lines to exactly the listed speakers / cloud only.
  8. Muted source: grey, no cloud.
  9. Click a speaker (solo pink noise) and drag a source in free 3D: both still work.
  10. Paint time: a project with 64 sources on a 32-speaker layout, `SPACER_STAGE_STATS=1`; record mean and max from the caption. Budget: mean ≤ 8 ms.

- [ ] **Step 5: `docs/ui-checklist.md`:** add the row `| WP15 3D stage view | [mac-validation/WP15.md](mac-validation/WP15.md) |`.

- [ ] **Step 6: Verify and commit**

Run: `grep -n "top-down\|toggle side view\|top/side" docs/SPEC-v1.md docs/DESIGN.md` — expected: no output.

```bash
git add docs/SPEC-v1.md docs/DESIGN.md docs/PLAN.md docs/ui-checklist.md docs/mac-validation/WP15.md
git commit -m "Describe the 3D stage view in the spec, design and checklists"
```

---

### Task 8: Audit, clean gate, pull request (controller + one opus reviewer)

- [ ] **Step 1: Read-only audit (opus).** Give the reviewer the spec, this plan and `git diff origin/main...HEAD`. Ask it to verify against the fetched JUCE source and `docs/SPEC-v1.md`, separate CONFIRMED from SUSPECTED, and check specifically: no JUCE include under `src/ui/logic`; no allocation-heavy work per paint beyond the dot lists; every existing stage assertion numerically unchanged (`git diff origin/main -- tests/ui/StageTransformTests.cpp tests/ui/StageHitTestTests.cpp tests/ui/StageSceneTests.cpp` shows only API respelling and additions); comments follow the comment rules; the designer's calls 1–17 are implemented as written. Intentional deviations to tell it up front: orthographic projection; `fitExtent` is sphere-based; Side view has no square grid.
- [ ] **Step 2: Prove two tests can fail.** Copy `src/ui/logic/StageTransform.cpp` aside, flip the sign of the pitch term in `orbit`, rebuild, confirm "Orbit turns 0.4 degrees per pixel…" goes red on the pitch assertion, restore, `cmp` the restore is byte-exact. Do the same in `StageScene.cpp` by removing the multiplication by level and confirming "Shares scale with level…" goes red.
- [ ] **Step 3: Clean gate.** `rm -rf build`, configure, whole-tree build, `ctest`. Expected as in "Build and test commands".
- [ ] **Step 4: Push and open the PR** (`gh` account `yishay-extra`; see the memory note on forcing gh's credential helper per push). PR body: what changed, the designer's calls 1–17 as a checklist for the owner, what is verified (unit tests, launch) and what is not (the look, checks 1–10 of `docs/mac-validation/WP15.md`, the paint budget). Do not merge.
- [ ] **Step 5: Hand the owner** the PR link and `docs/mac-validation/WP15.md`.
