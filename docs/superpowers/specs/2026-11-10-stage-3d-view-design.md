# 3D stage view — design

Date: 2026-11-10. Status: approved in conversation by the owner (look and
behaviour); items under "Calls made by the designer" are open to veto.

## Why

The stage (SPEC §16 item 1, `src/ui/StageView.*`) is a flat view with a
Top/Side toggle. Top hides height, Side hides left/right, so the three
coordinates of a speaker or a source are never visible together. The owner
wants:

1. speaker height readable at a glance while placing speakers, and
2. sources visible in space while they play, including which speakers carry
   them.

## What the owner decided

- One 3D view, used in both places the stage appears: the main Stage tab
  (`StageView::Mode::Perform`) and the layout editor (`Mode::LayoutEdit`).
- The view rotates by dragging. 3D is the default; Top, Side and Front are
  snap buttons on the same view and replace today's Top/Side toggle.
- The floor stays level while rotating (spin around, tilt up/down, including
  from underneath). The view never rolls.
- Every speaker and source above or below the floor has a thin stem to the
  floor, ending in a small hollow dot.
- Each source has its own colour, assigned automatically.
- A source is a cloud of dots at the source position. Louder = denser.
- Lines from the source to the speakers carrying it, plus a smaller cloud in
  the source colour on each of those speakers, both scaled by that speaker's
  share. Panning therefore reads as colour fading from speaker to speaker.
- The speaker level ring stays as it is.
- Ambisonic sources show the cloud only (they have no per-speaker gains).

Reference mockup (browser, throwaway, approximate panning): style "C · Both,
linked" of the session mockup. Its drawing code is not to be ported; it only
fixed the look.

## Goals

- Everything above, at the SPEC limits (32 speakers, 64 sources) without the
  UI becoming sluggish. Budget: mean `StageView::paint` ≤ 8 ms on the owner's
  Mac at 64 sources / 32 speakers (30 Hz repaint leaves 33 ms).
- All maths (camera, hit-testing, shares, cloud counts, colours, draw order)
  in `src/ui/logic`, JUCE-free and unit-tested on Linux, as today.
- Top and Side snaps behave exactly as today's Top and Side views: same
  screen positions, same hit-testing, same drag results. Existing assertions
  keep their numbers.

## Non-goals

- No OpenGL/Metal. Drawing stays `juce::Graphics`.
- No engine, command, feedback or project-file change. The camera is not
  saved in the project.
- No per-speaker shares for ambisonic sources.
- No user-chosen source colours, no colour chip in the Sources panel.
- No new pan gesture (zoom-about-cursor already shifts the view; double-click
  on empty space still refits).

## Anti-goals

- A view that looks good but lies: shares must come from the same spatializer
  classes the engine uses (`Vbap`, `Dbap`, `Direct` in `src/core/spatial`),
  never from an approximation.
- Snapped views that are almost-but-not-quite plans (see projection below).

## Calls made by the designer (owner may veto)

1. **Orthographic projection, not perspective.** No vanishing point. Top,
   Side and Front are then true plans, dragging maps screen pixels to metres
   exactly, and the existing transform API generalises without new cases.
   The mockup used mild perspective, so the app view will look slightly
   flatter. Depth is carried by stems, floor rings, draw order, and far items
   drawn a little smaller and dimmer.
2. **Dragging an item in a rotated view moves it in the screen plane**
   (parallel to the screen, through the item). This is today's rule ("the
   coordinate the view does not show is kept") generalised. Precise placement
   is done in a snapped view. Dragging empty space rotates.
3. **Side view loses its 1 m square grid.** All views draw the same floor
   (1 m rings, the two floor axes, "FRONT" label, listener head) plus a
   vertical mast at the origin with 1 m ticks as the height scale. In Side and
   Front the floor is a line.
4. **Front** = camera behind the listener looking forward: screen right is
   right, screen up is up.
5. **Colour by source id**, golden-angle hue steps, so a colour is stable when
   sources are reordered or others are deleted. Muted sources stay grey and
   draw no cloud, lines or glow.
6. **Cloud level** is the existing source meter (`FeedbackState::sourceLevelDb`,
   post gain and distance), mapped with `FeedbackState::meterFraction`. A
   silent source is just its dot. Both points of a stereo source use the
   source's one meter value.
7. **Particle budget.** At most 96 dots per source cloud, and at most 6000
   dots per frame across source clouds; when many sources are loud the
   per-cloud maximum shrinks (never below 24). The smaller cloud on a speaker
   uses a quarter of the per-cloud maximum. Dots are 2–3 px squares batched
   per cloud.
8. **An axis gizmo** (R / F / Up arrows) in the bottom-right corner.
9. **A paint-time readout** in the stage caption when the environment variable
   `SPACER_STAGE_STATS=1` is set, so the 8 ms budget can be measured.
10. **SPEC §16 item 1 and DESIGN.md are edited** to describe the 3D view. The
    project rule says a work package does not change spec decisions; this one
    is an owner-requested change, done as an explicit task with the exact new
    text in the plan.

11. **Shares use the source's base spread** (`SourceSpec::spread`). Spread
    modulation is applied inside the engine and is not reported back, so a
    modulated spread is not shown.
12. **Shares are computed once per source point** (two sets of lines for a
    stereo source), on every 30 Hz feedback tick, for unmuted points with a
    non-zero level. No cache. If the paint budget fails, that is reported,
    not hidden.
13. **Direct sources** draw lines from the dot to their listed speakers at
    gain 1, LFE speakers included (that is what the engine does, SPEC §6.4,
    §7). VBAP and DBAP never show a share on an LFE speaker.
14. **Hit-testing is unchanged**: nearest item to the cursor on screen, then
    the one nearer the viewer; sources win over speakers. Stems, clouds and
    lines are not clickable.
15. **Gestures.** Dragging empty space rotates in every view, including the
    snaps; after rotating, no snap button is highlighted. Snap buttons jump
    (no animation). Double-click on empty space refits zoom and pan and
    leaves the angle alone.
16. **Fit** uses the farthest speaker's distance from the listener (a sphere),
    so nothing leaves the view at any angle. For layouts on a sphere this is
    the same number as today.
17. **`StageProjection`, `setProjection()`, `projection()` are replaced**, not
    aliased. Nothing outside `StageView` and the stage tests uses them. Tests
    are respelled to the new API with their numbers untouched.
18. **Lines and speaker clouds are bounded** (added after the Task 6 review;
    supersedes the last two sentences of call 7 and narrows "lines to the
    speakers carrying it"). A source point draws lines to its 8 strongest
    speakers at most; the smaller cloud still appears on every speaker that
    carries it. All speaker clouds together aim at 3000 dots per frame: each
    gets `min(per-cloud maximum / 4, max(3, 3000 / number of source-speaker
    pairs drawn))`. The floor of 3 dots per pair wins past 1000 pairs, so at
    the limits (128 points x 32 speakers) the worst case is 12 288 dots.
    Reason: a wide DBAP source reaches all 32 speakers, and 64 of them would
    otherwise mean about 2000 lines and 47 000 dots per frame, far past the
    8 ms paint budget.

## Camera

State: `yawDeg` (any value, wrapped to (-180, 180]), `pitchDeg` clamped to
[-90, 90], plus the existing viewport, extent, zoom and pan.

With `y = yaw`, `p = pitch` in radians and world axes x right, y front, z up
(SPEC §2):

```
towardViewer d = ( sin y · cos p,  -cos y · cos p,  sin p )
screenRight  r = ( cos y,           sin y,          0     )
screenUp     u = r × (-d)
```

`plane(c) = (c·r, c·u)`, `depth(c) = c·d` (larger = nearer the viewer).
Screen = viewport centre + (plane − pan) · pixelsPerMetre, y flipped.

| Snap  | yaw | pitch | Shows |
|-------|-----|-------|-------|
| 3D    | −32 | 24    | default |
| Top   | 0   | 90    | right → +x, up → +y (front); depth = z |
| Side  | 90  | 0     | right → +y (front), up → +z; depth = x |
| Front | 0   | 0     | right → +x, up → +z; depth = −y |

Basis components smaller than 1e-6 in magnitude are set to 0 so the snaps are
exact. Orbit: yaw −= 0.4°·dxPixels, pitch += 0.4°·dyPixels.

## Shares

A JUCE-free helper in `src/ui/logic` owns one `Vbap` and one `Dbap` prepared
for the current layout (re-prepared when the layout changes) and answers "gain
per layout speaker for this source point" by calling `gainsFor(position,
spread, out)` — the method `core/Spatializer.h` documents as "for tests and
UI". `Direct` sources get gain 1 on their listed speakers. `Ambisonic`
sources report "no shares". The value drawn is `gain × level fraction`;
anything under 0.03 is not drawn.

## Drawing order

Background → floor (rings, axes, FRONT, mast, listener) → trajectories →
share lines → depth-sorted items, far to near (speaker: stem, glow clouds,
meter ring, box, label; source point: stem, cloud, dot, label) → gizmo,
caption, view buttons.

## Left to the builder

Exact colours and sizes, cloud shimmer, how far items shrink/dim, label
placement, internal class and function split inside the files the plan names.

## How it will be checked

- Unit tests in `tests/ui` for every logic piece (camera basis and snaps,
  round trips at arbitrary angles, orbit clamp, shares against `Vbap`/`Dbap`
  directly, budget, palette, draw order).
- A manual checklist `docs/mac-validation/WP15.md` on the Mac, including the
  measured paint time.
