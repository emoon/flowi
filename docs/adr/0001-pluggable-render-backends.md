# Pluggable render backends via a neutral render-command IR

## Status

accepted

Implemented. The Context below describes the codebase before this decision landed. Render backends and the
render-command IR are internal to the library, not public API.

## Context

flowi has only a software renderer, and it leaks throughout the system: `ui.c` calls
`sr_clay_to_render_commands()` directly, the SW `RenderCommand` struct (SIMD types, planar
glyph bitmaps, pre-split corners, `ImageData*`) is the de-facto interface, the painter
emits those same SW commands, fonts rasterize to CPU LCD/grayscale planes in the shared
`FontCache`, the public `FlImageData`/`FlDynamicImage` ABI exposes SW pixel layout
(padded/stride/deinterleaved), and the renderer vtable carries SW-only presentation
(`software_buffer`, DRM `set_output_target`). We want to add a GPU + SDF backend (modeled
on the Decima UIPainter: one draw call, SDF-in-shader, 8×8 tile culling, a zoom-regenerated
glyph atlas), and to build data-heavy audio UIs (synths, mixers) on top, which needs fast data-driven primitives
(waveforms, response curves, knob arcs, meters).

## Decision

Introduce a **high-level, backend-agnostic render-command IR** as the single boundary
between the UI core and pluggable **render backends**. Each backend owns its own
translation of the IR (SW: today's split/precompute/SIMD work, moved internal; GPU:
SDF/tile/atlas). Specifics:

- **Neutral IR carries intent, not baked data** — corner radii, border widths, gradient
  stops, clip push/pop, text runs, resource handles. Two tiers: *core* primitives every
  backend must render (rect/border/text/image/clip/gradient = today's features) and
  *optional* primitives/effects a backend may ignore (polyline+fill, arc/ring/circle/
  diamond/bar, drop shadow, glow). Optional primitives are the primary path for data-driven
  visuals — GPU-accelerated first, SW SIMD later.
- **Resources are referenced by stable, opaque UI handles** (`FlImageHandle`, `FlFontId`);
  each backend keeps a *private* map from handle to its **representation** (SW planar/
  padded pixels; GPU atlas rect). Representations never cross the IR boundary. The public
  image query returns status + logical dimensions only — a clean break of the
  `FlImageData`/`FlDynamicImage` ABI.
- **Fonts split**: a shared layer does FreeType face management, metrics, shaping,
  glyph indices, and **glyph-outline access**, and *never rasterizes*. Each backend
  realizes outlines its way — SW privately calls FreeType's hinted grayscale/LCD raster;
  the GPU rasterizes outlines on the GPU into a zoom-regenerated glyph atlas (Wallace),
  sampled by the uber shader.
- **SDF is a GPU-backend technique, never an IR concept.** The IR expresses shapes; the
  GPU realizes them via SDF, SW via its rasterizer.
- **Minimal renderer vtable** (originally `change_resolution`, `render`, `destroy`; it has since grown
  per-window surface lifecycle, the image/glyph preparation seams and an optional 3D seam, see
  `FlRenderBackend` in `flowi/src/ui/render_backend.h`). Surface/presentation is backend-specific and
  bound at the host boundary; pixel readback is an optional capability.
- **A backend is one coherent injected object** (renderer + image realization + glyph
  realization), constructed by the host and passed to `ui_shared_create(arena, backend)`, whose result is handed to each
  window's `ui_create`. SW and GPU can coexist in one binary.
- **Background becomes IR** — a backend-agnostic module holds crossfade/clear state and
  emits ordinary image/clear commands; the `sr_background_*` SW subsystem is removed.

See `CONTEXT.md` for the vocabulary. The Decima UIPainter, onedraw and Evan Wallace techniques are the
external references.

## Considered alternatives

- **Pass Clay commands straight to each backend (no new IR).** Rejected: leaks Clay into
  every backend and doesn't decouple the painter, which also emits SW commands today.
- **Backend mints its own resource ids.** Rejected: forces every call site to track and
  re-key backend ids across cache eviction/reload; reusing the existing opaque UI handles
  changes nothing for callers.
- **Shared layer rasterizes a neutral coverage bitmap for all backends.** Rejected: kills
  GPU SDF/outline benefits and SW LCD subpixel; outlines + per-backend realization keeps
  both.
- **Keep `FlImageData` ABI binary-stable.** Rejected in favor of a clean break since all
  plugin consumers are rebuilt from this repo.

## Consequences

- The SW backend must keep working throughout; its translation logic moves behind the IR
  rather than changing behavior.
- Widgets and the public handle API are largely unaffected (handles stay opaque and
  stable); the breaking change is the removal of SW pixel layout from public image types.
- The GPU backend can be added without touching widgets, and gets background + crossfade
  for free via IR.
