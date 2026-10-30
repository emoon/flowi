# Canvas paint participates in layout z-order (no global painter-on-top layer)

## Status

accepted

Implemented. The Context below describes the code before this decision landed.

## Context

Every data-heavy surface (timeline, piano roll, meters, device panels) is a
custom-painted canvas: it draws optional-tier primitives (polyline/arc/bar) from model
data and hit-tests sub-element zones that have no Clay elements. The framework needs a
substrate for those canvases.

There was exactly one path, and it was wrong for such surfaces. `painter.c` emitted the neutral
IR into its own per-frame arena, and `ui.c` appended that buffer
*after* all Clay-derived IR — so painter output was a single global layer that always drew
on top. Consequences: a canvas could never sit *under* a popup, context menu, or drag ghost;
the painter carried its own clip stack (capped at 16, silent overflow drop) divorced from
the layout clip stack; and the canvas had no layout rect of its own to hit-test against or
to scroll/clip with.

## Decision

A **canvas is an ordinary layout element that does inline painting within the element's
immediate-mode scope, replayed at the CUSTOM marker.** Painter calls between the element's
begin and end record optional-tier IR into that element's paint span; the first call lazily
attaches the span to the element's stock-Clay `CUSTOM` command (one marker per scope, never
per primitive — zero extra layout elements), and `fl_clay_to_render_ir` splices the span
into the frame IR at that command's position. Therefore the canvas:

- draws in **element-local coordinates** (origin = its rect), not window coordinates;
- is **bracketed by the live layout clip/scissor stack** — the painter's private depth-16
  clip stack is gone;
- draws **in z-order at its element's position** (under its child elements), so floating
  elements declared later (popups, menus, drag ghosts) compose *over* it correctly.

The "painter as a separate end-of-frame buffer appended on top" model is removed. `painter`
becomes the *emit API used inside an element's open scope*, not a standalone on-top layer.

### Amendment (2026-06-20): intra-element z-order and own-clip

The original decision fixed the painter's z-order *relative to other elements* but left two
intra-element details to stock-Clay emission order, both of which were wrong:

- Stock Clay emits an element's `CUSTOM` (paint span) **before** its `RECTANGLE`
  (`color:` background), so a non-transparent `color:` overpaints the canvas. Any element
  wanting "solid fill + painter decoration" had to drop `color:` and paint the fill itself.
- Stock Clay emits `CUSTOM` **before** the element's own `SCISSOR_START`, so the paint span
  was clipped only by *ancestor* clips, not the painting element's own bounds — paint could
  bleed past its own edge until an ancestor clipped it.

Decision: the per-element z-stack is fixed, unconditionally (no opt-in flag), to
**`background (color:) → own scissor → paint span → children → border`**. The element's own
background draws first, then the painter decoration draws over that fill but under children,
and the whole paint span is **clipped to the painting element's own rect**, not just its
ancestors'. This is done as a `flowi divergence` reorder in the vendored Clay (see ADR-0012);
`fl_clay_to_render_ir` remains a faithful pass-through. No new API surface — `color:` and
painter calls now simply compose the obvious way.

Rejected: an opt-in "paint under background" flag — a permanent per-element knob serving a
case with no real use (if you want paint under the fill, omit `color:`).

## Considered alternatives

- **Painter layers with an app-managed z key.** Rejected: reintroduces a parallel
  z-ordering system the layout engine already solves; the app would hand-maintain z values
  that layout assigns for free.
- **Status quo (painter-on-top, app positions canvases manually).** Rejected: nothing can
  float over a canvas — broken the moment a clip needs a right-click context menu, or a
  drag ghost crosses the timeline.

## Consequences

- `painter.c`'s appended-at-frame-end model goes away; it records per-element spans in its
  own arena that `fl_clay_to_render_ir` replays at each element's `CUSTOM` marker so paint
  interleaves at the right z. This is a real change to how `painter.c` relates to `ui.c`'s
  IR assembly. (Coordinates are still scaled to pixels at emit; the splice adds the
  element's resolved rect origin — element-local → window translation lands at the seam.)
- Hit-testing a canvas uses its layout rect + element-local `mouse_pos`; this is the
  foundation sub-element hit-testing builds on.
- Kills both defects (z-interleaving and the depth-16 painter clip cap) in one move.
- The world↔pixel transform a canvas needs for zoom/pan layers *on top of* this
  element-local frame; it is a separate decision (see ADR-0010).
