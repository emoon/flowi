# GPU SVG: partial per-icon re-raster on zoom, not wholesale regeneration

## Status

accepted (implemented in the SVG atlas and covered by `gpu_smoke_test`)

## Context

ADR-0007 set the glyph atlas's zoom policy: on a UI-scale change every glyph is
forgotten and the live set repacks from the origin (wholesale regeneration, no
per-glyph eviction). That fits text — a scale change moves *every* glyph onto a
new pixel size at once, there are hundreds of small cells, and repacking them
together keeps the skyline tight.

GPU vector icons (GPU SVG, CONTEXT.md "Vector Image") share the atlas machinery —
the same skyline packer, the same compute coverage dispatch, an R8 cell per
prepared item — but their zoom behavior is the opposite shape:

- **Icons re-raster independently, continuously.** A host zooming one panel
  re-sizes a handful of icons while the rest hold still; there is no single
  "scale changed, everything moves" moment the way a UI-scale step is for text.
  An icon's physical `dst` size tracks zoom continuously, quantized onto a size
  ladder (`svg_quantize_size`) so it re-rasters only at step boundaries.
- **Few, large cells, often oscillating.** Icons are large relative to glyphs and
  there are far fewer of them, and a user nudging a zoom slider crosses the same
  step boundary back and forth — so cell reuse across a zoom gesture matters more
  than packing density.

Forgetting the whole atlas every time one icon crosses a step (the ADR-0007
discipline) would re-rasterize every *other* live icon for no reason, and an
oscillating zoom would thrash the entire atlas on every frame the slider wiggles.

## Decision

The SVG atlas diverges from ADR-0007: **re-raster is incremental and per-icon**,
not wholesale.

- **One live cell per handle, append-and-orphan.** `prepare_vector` is keyed by
  `(handle, quantized size)`. When an icon's quantized step changes, a fresh cell
  is appended and the prior cell is orphaned (left in place, reclaimed only by a
  later wholesale regeneration); the handle resolves to its single current live
  cell for drawing. Within a step, `prepare_vector` resolves to the live cell and
  dispatches nothing — re-preparing every build is idempotent.
- **A crossing rasters exactly one cell.** Only the crossing icon's cell is
  dispatched; every other icon's cell — placement and texels — is untouched. The
  continuous-display decoupling is free: the cell rasters at the quantized size,
  the `FlCmdImage.dst` draws it at the exact physical size via linear sampling
  (crisp at each step, slightly soft between).
- **Quantization is on the uniform size.** The larger destination side drives the
  ladder, matching the prepare seam's aspect-fit of the icon's larger document
  side. A non-uniform stretch (a `dst` whose aspect differs from the icon's) is
  **not** re-rastered per-axis: the single cell is sampled and stretched, an
  accepted softness.
- **Wholesale regeneration is retained only as the fallback**, on the same two
  triggers as the glyph atlas: a UI-scale change (every icon moves onto new
  quantized sizes at once — the text-shaped case) and an atlas-full repack. Then
  the live set re-prepares from the origin, exactly like ADR-0007.
- **Format-agnostic lifecycle.** The live-cell / quantize / orphan bookkeeping is
  parameterized by texel format (R8 today; RGBA8 for multi-color icons later), so
  the second atlas reuses this policy rather than forking it.

## Consequences

- An icon's atlas footprint grows by one orphaned cell per distinct step it has
  visited since the last wholesale regeneration. This is bounded by the ladder
  (a handful of steps) and reclaimed on the next UI-scale change or atlas-full
  repack — the deliberate trade for not thrashing the whole atlas on every
  per-icon zoom crossing.
- Behavior diverges visibly from the glyph atlas under instrumentation: the
  raster-dispatch count ticks once per icon per step crossing and stays flat
  while a host zooms within a step (asserted in `gpu_smoke_test`), whereas the
  glyph atlas re-rasters its whole live set on each scale step. This divergence
  is intended; it is the reason this ADR exists.
- The orphan-on-step-change cells are dead until reclaimed — they are never
  sampled (the handle resolves only to its live cell), so they cost atlas space,
  not correctness.

## Amendment (2026-06-13): grow the atlases, extend the ladder, asymmetric per class

The quantized size ladder originally capped at 512 (`k_svg_size_ladder`), so an
icon displayed larger than ~512px sampled a stretched 512² cell and **stopped
getting sharper** while the rest of the scene kept scaling crisp. The cap existed
only because a 512² cell is already a quarter of the shared 1024² atlas and a
1024² cell would *be* the whole atlas.

The ceiling is raised by growing the atlas and extending the ladder — note the
texture bump alone does nothing, the *ladder* is the ceiling. The two atlases
grow **asymmetrically** because RGBA8 is 4× the bytes of R8 and giant crisp
*multi-color* icons are far rarer than mono ones:

- **Mono (R8) atlas → 4096²** (16 MB), ladder extended to ~2048.
- **Multi-color (RGBA8) atlas → 2048²** (16 MB), ladder/cap to ~1024.

`svg_quantize_size` therefore clamps to a **per-class** maximum (by
`FlVectorClass`): a color icon stops at 1024, a mono icon at 2048.
`GPU_SVG_ATLAS_MAX_ENTRIES` and the packer's node capacity grow to match the
larger textures.

Trade-off: ~32 MB of VRAM (16 MB mono + 16 MB color) buys a much higher sharpness
ceiling. **Considered and rejected:** a dedicated full texture for oversized
icons (mirrors the large-image path, crisp at any zoom, but more code than the
deep-zoom-on-chrome case warrants today); and a uniform both-4K bump (the 64 MB
RGBA8 atlas is not worth it where huge crisp multi-color icons are rare). If a
real app needs multi-color icons crisp beyond 1024px, revisit the dedicated-
texture option rather than growing the shared color atlas further.

