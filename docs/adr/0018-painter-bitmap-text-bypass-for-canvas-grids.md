# The painter bitmap-text path deliberately coexists with the font_cache/IR path

## Status

Obsolete. The bitmap-font type, `font_add_bitmap_1bit`, the painter bitmap char/text calls and `bitmap_font.c` no
longer exist in the code, so the path described below has been removed. The painter's text calls (`Painter::text`,
`text_styled`) go through the font_cache/IR path. The body is kept as historical rationale.

## Context

flowi had two ways to put a bitmap-font glyph on screen, and at a glance they look like
duplication — a structural audit flagged exactly that and proposed retiring one:

- **The canonical path** — `font_add_bitmap_1bit` registers a 1-bit font with the
  `font_cache` (as a `ScaledFontSet`), and glyphs cross the neutral IR through the image
  primitive's **alpha-coverage** mode (the IR home for bitmap glyphs, per `CONTEXT.md`).
  This is the path ADR-0001 wants everything visible to flow through.
- **The painter bypass** — a bitmap-font type (a pre-scaled grayscale atlas built from a 1-bit
  charset) drawn via painter character and text calls,
  emitting directly from inside a [[Canvas]] paint scope, *outside* the alpha-coverage
  contract.

The audit only saw flowi in isolation, where the bypass has no caller but a unit test, and
concluded it was dead duplication. A downstream application tells a different story: one of its plugins
renders dense **pattern grids** entirely through the painter bitmap-text call — note
cells, row numbers, instrument/effect columns, headers — with fonts built from a 1-bit charset. The bypass is load-bearing
production code, not debt.

## Decision

Keep both paths. The painter bitmap-text path is the supported way to draw bitmap text at an
**arbitrary `(x, y)` inside a canvas paint scope**, and is retained as public API.

The two paths serve genuinely different jobs:

- The font_cache/IR path serves **layout text runs** — text positioned by the layout engine,
  carrying an `FlFontId`, measured for layout.
- The painter bypass serves **dense, immediate-mode, self-positioned bitmap text inside a
  canvas** — a tracker grid computes each cell's pixel position itself and redraws thousands
  of cells every frame. A pre-scaled atlas drawn directly is both the natural fit (the canvas
  already owns its coordinate math) and the fast path (no per-glyph cache/coverage resolution
  on a per-frame-dense grid).

## Considered alternatives

- **Retire the bitmap-font type and the painter bitmap char/text calls; route everything through
  `font_cache`** (the audit's proposal). Rejected as a *cleanup* item: it is not a deletion,
  it is a cross-repo migration of a shipping, pixel-sensitive downstream plugin. The painter does carry a
  general `fl_painter_text(x, y, FlString, FlFontId, color)`, and `UiCtx` already holds
  `ScaledFontSet` "for pattern displays," so such a migration is *theoretically* possible —
  but it is a deliberate project (re-point that plugin onto scaled font handles + verify
  the tracker grids pixel-for-pixel + confirm the per-frame cost), not a mechanical cleanup.
  If undertaken, it should be its own project, not folded into a dedup pass.

## Consequences

- ADR-0001's "everything visible flows through the neutral IR" has one documented exception:
  the painter bitmap-text bypass. It is intentional, scoped to canvas-local bitmap grids, and
  recorded here so a future reader does not "fix" it by deleting a path the tracker views
  depend on.
- The 1-bit→grayscale unpack is still written more than once across the two paths
  (`bitmap_font.c`, `bitmap_font_1bit.h`). Collapsing it into one shared
  `unpack_1bit_to_gray` helper remains worthwhile and is independent of this decision — it
  dedups an implementation detail without merging the two paths.
- If the per-frame cost of the canonical path is ever shown (by measurement) to match the
  pre-scaled atlas for dense grids, this ADR should be revisited and the migration above
  reconsidered.
