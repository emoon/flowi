# Fork Clay into an owned layout engine

## Status

Superseded by ADR-0075. The rename fork described below was never carried out; Clay stays vendored under
`external/clay/` with its `Clay_*` symbols and a small set of documented local patches. The body is kept for its
rationale.

## Context

flowi uses [Clay](https://github.com/nicbarker/clay) (v0.14, zlib license) as its
layout engine, vendored pristine under `external/clay/` and compiled with `-w`. Two
translation seams bracket it:

- **Input:** `ui/layout.c` translates the public layout config and sizing structs into
  `Clay_ElementDeclaration`. Those structs are deliberate 1:1 mirrors of Clay's
  layout structs, kept ABI-identical by a wall of `_Static_assert`s on sizes and offsets.
- **Output:** `ui/clay_to_render_ir.c` turns `Clay_RenderCommand[]` into the neutral
  `Fl*` render IR (ADR-0001).

The input mirror existed only because the public vocabulary was a legacy consumer-prefixed set (a frozen
plugin ABI of an earlier host application) while the engine speaks `Clay_*`.
ADR-0003 already committed flowi to migrating off that set to its own prefixed types (now `Fl*`,
ADR-0031) and to rejecting new consumer-prefixed types. Separately, an application with many panels would
eventually want layout behaviour Clay does not offer (e.g. virtualization for the thousands of elements in
a large scrolling view), which means editing the layout algorithm itself — impossible
while Clay is a pristine upstream drop.

The translation overhead is not a performance problem. The motivation is **ownership**:
shedding the foreign type vocabulary and unlocking the ability to extend the algorithm.

## Decision

Fork Clay into a flowi-owned engine, **starting with the layout-struct slice only**.

- **Light fork.** The only change to the engine is a mechanical `Clay_* → Fl*` rename
  (layout config, sizing, bounding-box and layout-command types). The
  layout algorithm is untouched, so the fork stays re-baseline­able against a future Clay
  via a rename script — until the *first* deliberate algorithm edit, which is a separate,
  individually-justified one-way door.
- **`Fl*` low-level tier, not the widget-API tier.** Layout value-types belong in flowi's low-level tier alongside
  the render IR (ADR-0003's ImGui analogy: `ImDrawCmd` is `Im`, not `ImGui`). The
  widget-API naming sweep is unrelated and later.
- **Attribution, not erasure.** The `Clay_`/`Clay__` symbol prefixes go, but Nic Barker's
  zlib copyright notice stays, with an added marker that the file is an altered fork of
  Clay v0.14 — as the license requires.
- **Scope fence: layout structs only.** In scope: `Sizing`, `Padding`, `Direction`,
  `Alignment`, `SizingType`, `ChildAlignment`, `LayoutConfig` — collapsing the legacy layout
  mirror into one `Fl*` set and deleting the static asserts. Out of scope: value types.
  `Clay_Color` stays float internally and `clay_to_render_ir.c`'s `to_em_color` fold
  remains on the output side; the pervasive legacy-prefix color/string/font-id types → `Fl*`
  migration is a separate, larger effort.
- **Files.** Engine → a flowi-owned layout engine source/header pair under `ui/` (owns the layout
  type definitions); `external/clay/` is deleted. `ui/layout.c` / `ui/layout.h` remain as the
  thin flowi authoring skin — they shrink (struct translation collapses to direct
  assignment) but keep UI-scale, pixel-snapping, scroll resolution, debug duplicate-ID
  detection, and the public `ui_layout_*` entry points. The engine keeps `-w` (or a
  scoped pragma) for now and is brought under the warning regime incrementally.

The slice is safe to land unilaterally: nothing outside flowi writes the layout config — app code drives flowi
through widgets.

## Considered alternatives

- **Keep Clay pristine, keep the mirror.** Rejected: leaves flowi welded to a foreign
  type vocabulary and forecloses any future algorithm change — the actual goal.
- **Fork Clay to speak the legacy consumer prefix.** This would delete the mirror outright, but bakes the
  legacy *consumer* prefix into flowi's own engine, directly contradicting ADR-0003.
  Rejected.
- **Fork to `Fl*` but migrate value types (color/string/id) in the same pass.** Rejected:
  `Clay_Color` is float-0–255 vs `FlColor`'s `u8×4` — a representation change that reaches
  into Clay's text/render internals and turns a light rename into a deep fork, while the
  output-side conversion remains anyway. Deferred to the standalone value-type sweep.
- **Replace Clay with an in-house layout engine.** Rejected for now: Clay's algorithm is
  fine; owning a renamed copy gets the extensibility without rewriting a working engine.

## Consequences

- flowi owns its layout engine. Clean upstream pulls end the moment the algorithm is first
  edited; until then a rename script can re-baseline against a newer Clay.
- The legacy layout structs become unused by flowi (flowi simply stops referencing them for layout).
- This is the first concrete legacy-prefix → `Fl` slice under ADR-0003, peeled off ahead of the full
  sweep because the fork creates a natural reason to. The remaining value-type
  migration is unaffected and still pending.
- A later algorithm extension (e.g. virtualization) is now possible inside the owned engine;
  each such edit is its own decision, not licensed wholesale by this ADR.
