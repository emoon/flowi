# GPU glyphs: compute coverage from light-hinted outlines, grayscale only

## Status

accepted

## Context

The shared font layer hands backends glyph *outlines*, never bitmaps
(ADR-0001); the GPU backend never CPU-rasterizes. The reference technique
(Evan Wallace) accumulates per-curve signed areas via additive blending into a
float render target — designed before compute was a given. The SW backend
renders FreeType-hinted grayscale/LCD bitmaps, which small UI text leans on
for crispness.

## Decision

- **Rasterization is one compute dispatch**: one thread per atlas texel loops
  the glyph's curve segments from an SSBO, accumulates winding/signed
  coverage (Wallace's math, compute execution), writes **R8 coverage**. No
  draws, no float blend target; the backend's only non-compute GL work stays
  the final blit (ADR-0006).
- **Outlines are light-hinted**: FreeType autohinter, `FT_LOAD_TARGET_LIGHT`
  (vertical stem snapping only), evaluated at the target pixel size for the
  current zoom. Recovers most small-size crispness while staying
  GPU-rasterized.
- **Atlas policy**: single R8 atlas, skyline-bottom-left packing on the CPU
  (per the Decima talk), glyphs appended incrementally via `prepare_glyph`,
  **regenerated wholesale on zoom change** (and on grow/full) — no per-glyph
  eviction, matching the seam's "no release_glyph" contract.

## Consequences

- **GPU text is grayscale-AA only — LCD subpixel is knowingly dropped** on
  this backend (SW keeps it). Text will render slightly differently between
  backends; this is deliberate, not a bug. RGB-stripe output from a compute
  uber-shader is not worth the complexity, and grayscale at editor sizes is
  proven by Decima.
- Hinted outlines are size-dependent, so a glyph's outline is fetched per
  (font, size × zoom), not once per glyph — already the shape of the
  `prepare_glyph` seam.

## Amendment (2026-06-13): the wholesale wipe is deferred to a frame boundary

The wholesale regeneration above (forget every placement, repack from the
origin) is kept, but it must not fire *inside* `prepare_glyph`. During a zoom-in
the atlas fills (old quantized sizes accumulate, never evicted), and wiping
inline mid-build forgets glyphs the app has *already* prepared this same frame —
they then resolve to NULL at draw and blank, which reads as flicker that recurs
every few zoom steps.

So overflow now **sets a `needs_wipe` flag and the atlas is reset at the end of
`render()`** — the only backend-controlled point between this frame's draw and
the next frame's prepare pass (there is no backend `begin_frame` seam). The next
frame's normal, full prepare pass repopulates the bounded live set into a fresh
atlas, so nothing blanks. The packer stays append-only — no per-glyph eviction,
no retained outlines, no frame-stamping; overflow is a sufficient trigger. The
only visible cost is that a glyph appearing for the first time on the exact
overflow frame is absent for that one frame.

## Amendment (2026-06-13): grow the glyph atlas to 4096²

The deferred wipe above only helps when the live set fits a fresh atlas. The
raster-size ladder (`font_cache_quantize_size`) is unbounded, so zooming in grows
a glyph's cell without limit. At the old 1024² a glyph rasterized past ~1022px
could not be placed in even an *empty* atlas — it overflowed and blanked, so
zooming hard on a single character made it vanish.

The glyph atlas therefore grows to **4096² R8 (16 MB)**, mirroring ADR-0016's
SVG-atlas growth. This raises the single-glyph ceiling to ~4094px, covering any
realistic single-character zoom. The ladder is still unbounded, so a glyph zoomed
past the new ceiling would vanish again; if that ever bites, clamp the glyph
ladder to the atlas capacity (as ADR-0016 clamps the SVG ladder) and let the
per-glyph display scale stretch the cell past the cap — softening rather than
vanishing. The grow is the chosen fix today; the clamp is the documented
follow-up.

## Amendment (2026-06-13): the zoom flicker was host-side, not capacity

A frame-stamped wholesale eviction was implemented here and then **reverted**. It
was built on the theory that the dominant zoom flicker came from dead quantized
sizes accumulating until the atlas overflowed and wiped. That theory was wrong.

The real cause lived in the *host*, not this module. A host that re-prepares text
every frame must only prepare the glyphs it actually draws **on screen**. The
`examples/gpu_window` host prepared the entire scene's text every frame regardless of
the pan/zoom viewport, so at high zoom it packed dozens of off-screen glyphs at
their full zoomed pixel size into the atlas — overflowing the *spatial* packer
(not the entry table) and silently dropping the few on-screen glyphs, which
flickered. The packer's spatial-overflow rejection logs nothing, which is what
made this hide behind two capacity-shaped fixes.

The fix is **host-side viewport culling**: skip preparing/emitting any glyph whose
pen falls outside the visible world region (the inverse of the view transform).
With the working set bounded to what's visible, the append-only atlas + deferred
wipe is sufficient and frame-stamped eviction is unnecessary complexity — hence
the revert. The contract this module assumes is unchanged: **a glyph must be
prepared the frame it is drawn.** A host that draws without culling can still
overflow the spatial packer; that remains a silent drop, and the clamp follow-up
above is the durable backstop.

