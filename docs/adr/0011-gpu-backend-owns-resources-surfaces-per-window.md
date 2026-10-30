# GPU backend owns shared resources; surfaces are per-window (multi-window shape adopted early)

## Status

accepted

Implemented: the single `GpuRenderData` was split into `GpuShared` (backend-shared) and `GpuSurface`
(per-window) in `flowi/src/render/gpu/gpu_renderer_internal.h`; `FlRenderBackend` creates and destroys surfaces and
`render`/`change_resolution` take a surface (`flowi/src/ui/render_backend.h`). The glyph atlas and image cache now
exist and live in `GpuShared`. The Context below describes the code before the split.

## Context

Multiple windows are a hard requirement — a detachable instrument / Live-style second
window and native plugin-editor windows. ADR-0001 already says a render backend
is one coherent object; ADR-0005 puts it on OpenGL via SDL2; ADR-0006 renders into an
offscreen RGBA8 tile-raster target.

At the time, `GpuRenderData` (one backend object) mixes two lifetimes:

- **resolution-independent**: the `classify`/`raster` compute programs and the per-frame
  `cmd`/`aabb`/`clip` SSBOs (rebuilt each frame, one frame in flight);
- **per-surface, currently singular**: `target_tex` (offscreen RGBA8), `readback_fbo`,
  `tile_ssbo` (sized to the resolution's tile grid), `readback_buf`, `screen_width/height`.
  There is exactly one, and `render`/`change_resolution` act on it — a single-window
  assumption.

Critically, the *expensive* shared resources — the glyph atlas and the image texture
cache — **do not exist yet** (the current slice renders solid fill rects only). Retrofitting
shared resources after they are tied to one window's context/swapchain is the costly
reversal; born in the shared tier, they cost nothing.

## Decision

**One `RenderBackend` owns shared resources; multiple surfaces are rendered from it.**

- The GPU backend has two explicit resource tiers:
  - **backend-shared** (one per backend, created at construction): GL function table, the
    compute programs, the per-frame command SSBOs, and — when implemented — the **glyph
    atlas** and **image texture cache**.
  - **per-surface** (one per window): the offscreen RGBA8 target, its readback FBO, the
    per-tile classification SSBO sized to that surface, the CPU readback buffer, and the
    surface's dimensions / tile grid.
- **One GL context drives N window surfaces.** `render` and `change_resolution` act on a
  surface; resource preparation (atlas, images) is backend-shared and never tied to a
  swapchain. Presentation stays host-side per ADR-0001.
- **Adopt the shape now, defer the plumbing.** Split the struct now; do not build actual
  multi-window window management yet. Cheap now precisely because the backend is tiny and
  the heavy shared resources are unwritten.

## Considered alternatives

- **Per-window backend instances with shared GL contexts**
  (`SDL_GL_SHARE_WITH_CURRENT_CONTEXT`). Rejected: duplicate atlas/image caches to keep
  coherent, cross-context object-sharing rules, more lifecycle — for no benefit at this
  draw-call count.
- **Stay single-window, split later.** Rejected: the atlas and image cache would bake into
  window/context ownership before the split, which is exactly the reversal to design out early.

## Consequences

- `GpuRenderData` splits into a shared backend object + a per-surface object; the renderer
  seam's `render`/`change_resolution` gain a surface parameter (or operate on a surface
  handle) — a seam change made while the backend is small.
- The glyph atlas and image cache, when added, go in the shared tier from day one.
- The SW backend keeps the same logical split (its resource cache is already
  backend-owned; its "surface" is the output buffer) — kept symmetric so the IR seam stays
  backend-neutral.
- The offscreen tile-raster target (ADR-0006) is per-surface-sized; one backend may hold
  several at different resolutions.
