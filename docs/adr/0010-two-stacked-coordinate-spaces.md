# Two stacked coordinate spaces: logical-pixel UI scaled once, per-canvas content transform beneath

## Status

Partly superseded by ADR-0020, and partly not implemented.

- Superseded: `window_dpi_scale` no longer exists; the core takes one absolute UI scale via
  `ui_set_scale(ui_scale, text_scale)` and the app composes DPI and UI-zoom.
- Not implemented: the per-canvas content transform. The current code has no content-zoom transform; that half of the
  decision is intent only.

## Context

A UI toolkit has two things people both call "zoom," and they are not the same transform:

1. **DPI / UI-zoom** — the whole UI getting bigger on a high-DPI monitor, or via a user
   "UI scale" preference (many applications ship one). Uniform, global, affects every
   widget and canvas. When this was written, `window_dpi_scale` was declared but never assigned.
2. **Content zoom** — zooming a timeline far along its axis, or per-row vertical zoom in a
   multi-row editor. Per-editor, often non-uniform per axis, anchored around the cursor, and it must
   scale *content* (clips, notes, waveforms in an editor) while leaving *chrome* (ruler labels, track
   headers, clip-name text) at normal size. There was no zoom concept.

Conflating them is a concrete bug: if content zoom were applied as a global on-top scale,
ruler and label text would balloon with the content.

We also had to choose the authoring/reference space for the global scale: a **fixed design
resolution** (author at 1920×1080, scale to fit any window — the game/console model), or
**logical (density-independent) pixels** (a bigger window gives more pixels to lay out
into — the desktop-app model).

## Decision

Two coordinate transforms, stacked, with distinct owners:

- **UI authored in logical pixels.** 1 logical px ≈ 1 physical px at 100% DPI / 100%
  UI-zoom. Window size and DPI determine how *many* logical pixels a surface has — a bigger
  or higher-DPI window yields **more** content, not a stretched layout. **Fixed design
  resolution is rejected** — it would waste large monitors, and users of dense editors want more
  content on screen.
- **One UI scale** composes monitor DPI × fractional-DPI × user UI-zoom preference into a
  single uniform multiplier, applied once to the whole frame. This is the home for the dead
  `window_dpi_scale` (since replaced by `ui_set_scale`, ADR-0020).
- **A per-canvas content transform** maps world units (beats/semitones/seconds) → logical
  pixels, applied at paint-emit time and shared in inverse by hit-testing. It may be
  non-uniform and anchored, and it scales content only — chrome stays at UI scale.

They compose as `physical_px = UI_scale · element_origin · content_transform · world_pt`.
The framework stays agnostic about what the content axes *mean* (it knows "an affine," not
"beats").

## Considered alternatives

- **One global zoom for everything (fixed 1080p design res, scale to fit).** Rejected:
  blows up chrome text with content, wastes large monitors, and contradicts the desktop-app
  expectation that a bigger window shows more.
- **Content zoom kept entirely app-side, framework knows only pixels.** Deferred to the
  content-zoom design, but the transform must be shared by paint *and* hit-testing, so the framework is
  at minimum the place that composition happens; the app owns axis meaning, not a private
  second copy of the matrix.

## Consequences

- `window_dpi_scale` gets reanimated as part of the UI scale; runtime UI-zoom comes
  along for free.
- Canvas content zoom is a separate, later design — non-uniform/anchored affine API,
  pixel-snapping at fractional offsets — but it now has a defined place in the stack.
- The neutral IR stays pixel-based (transform applied at emit); pushing a transform into
  the IR for the GPU to apply is a possible future optimization, explicitly not adopted now
  (keeps the ADR-0001 IR-carries-intent-not-baked-data boundary clean).
