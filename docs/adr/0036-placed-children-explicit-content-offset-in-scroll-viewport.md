# Placed children: explicit content-space offset inside a scroll viewport

## Status

accepted

## Context

Coordinate-driven surfaces — timeline clips, piano-roll notes (x=time, y=pitch), automation
points/curves, gantt bars, node graphs — place interactive objects at model-derived
positions *inside a pannable/zoomable, clipped viewport*. Today the only way to position a
child at an explicit coordinate is the **spacer trick**: emit a `fixed!(gap)` element, then
the real element, threading a cursor accumulator. That works but:

- is **1D only** — there is no "spacer in two axes," so a piano roll (x *and* y) cannot be
  expressed at all;
- **cannot express overlap** — two objects at overlapping coordinates;
- requires **sorted input** and a threaded accumulator, and writes "place at X" as "emit a
  gap of `X − cursor`."

We evaluated reusing the existing **floating** mechanism (Clay `CLAY_FLOATING`, exposed as
`floating!` / `fl_floating_*`). It is the wrong tool, confirmed against the engine:

- A floating child is positioned from its attached parent's **static bounding box**
  plus `offset`. The parent's scroll vector (`clip.childOffset`) is
  re-added to a floating root **only** inside the `externalScrollHandlingEnabled` branch of Clay's
  position pass, which flowi keeps disabled. So a floating child **pins to the
  container frame and does not scroll with content**.
- Floating children are resolved as **separate layout tree roots** (own resolution pass,
  pointer-capture handling, separate hit-test pass). One root per object does not scale to a
  piano roll's object count.
- It overloads the glossary: *floating* is reserved for **overlays that compose over content
  and overflow** (popups, menus, drag ghosts), not content that scrolls and clips with the
  view.

Flowed children, by contrast, already get `+ scrollOffset` in Clay's position pass and the
container's own scissor for free. The thing we want — explicit offset + scrolls with content
+ clipped to the container — is reachable *only* as a flowed child.

## Decision

Add a **placed child**: a layout element positioned at an explicit `(x, y)` offset in its
parent's **content coordinate space** (the same space `clip.scroll_pos` / `childOffset` lives
in), instead of by flex flow. A placed child:

- **scrolls with the container** and is **clipped to the container's active area** — it is a
  flowed child for scroll/scissor purposes;
- **does not advance the flex cursor** and **does not contribute to content extent** — it is
  pure placement. The app owns content extent, scroll-clamping, and **virtualization**
  (emitting only the objects in the visible range); engine offscreen culling is the backstop,
  not the primary scaling mechanism;
- is **a real interactive element** — `item_behavior`, focus, hit-testing, and the
  [[Placed child]] z-slot all work exactly as for any element. Per CONTEXT.md "custom
  painting is orthogonal to interaction," objects you press (clips, notes) are elements;
  DIY-painted hit-testing stays the measured fallback for sample-dense content only.

Mechanism: a `flowi divergence` in Clay's flowed-child positioning (`external/clay/clay.h`,
`Clay_FlowiPlacedConfig` / `Clay__ElementIsPlaced`; surfaced in the layout API as `LayoutPlaced`,
`api/layout.def`) — when a child carries an explicit placement offset, use it as the child position (still inside
the container's scissor and `+ scrollOffset`) instead of `nextChildOffset`, and skip the
cursor advance and the extent contribution. Offsets are in already-zoomed reference pixels (the
app bakes content zoom into the coordinate, per ADR-0020 / the content transform), so the
engine never needs to know about zoom.

## Considered alternatives

- **Reuse `floating` (attach-to-parent + offset + expose `clip_to`).** Rejected: floating
  pins to the static parent box and does not track scroll without enabling
  `externalScrollHandlingEnabled` globally (broad side effects); one tree root per object does
  not scale; and it overloads the overlay-only meaning of *floating*.
- **Ergonomic wrapper over the spacer trick.** Rejected: stays 1D, cannot express overlap,
  keeps the accumulator — it papers over the workaround without unblocking the piano roll.
- **Placed children contribute to content extent (engine measures the bounding box).**
  Rejected: redundant with the app's model-driven extent, and fights virtualization (you'd
  need every object present to measure).

## Consequences

- New layout concept and a Clay positioning-DFS divergence (the largest of the changes
  made for this surface), but it is the load-bearing primitive for every 2D coordinate-driven surface; the
  timeline becomes a free beneficiary (the spacer trick + accumulator
  collapse to a stateless loop).
- Placed children participate in engine offscreen culling like any flowed child, so the app's
  virtualization has a correctness backstop.
- A placed child still composes under its container's later siblings / floating elements,
  exactly like a flowed child — z-order is unchanged.
- Does **not** add a "timeline" or "piano roll" abstraction; the engine stays domain-neutral
  (it only learns "place this child at this content offset").
