# GPU backend rasterizes and classifies tiles in compute, not fragment

## Status

accepted

## Context

Decima's shipped UIPainter draws one fullscreen triangle whose fragment shader
walks a per-tile command list (tiles classified on the CPU); moving
classification to the GPU was their stated future work. We had to choose for
our GL 4.3 backend (ADR-0005): fragment raster like they shipped, or compute
for both passes. At UI command counts the frame-time difference is
second-order either way.

## Decision

Both passes are **compute shaders**:

- **Classification:** one thread per 8×8 tile brute-force tests every draw
  command's AABB, writing a fixed-capacity per-tile index list (64 entries,
  overflow counter + heat-map debug view, per the Decima war story).
- **Raster:** one 8×8 workgroup per tile (workgroup = tile), command list
  cooperatively staged through shared memory, register blending, single
  `imageStore` per pixel into an offscreen RGBA8 texture carrying real
  coverage alpha (GL cannot image-store to the default framebuffer). The
  host's GL display backend composites that texture over the core display
  plane and presents — the offscreen hop is absorbed by the composite pass,
  and presentation stays out of the render backend per ADR-0001.

Deciding reasons, in order:

1. **GLES 3.1 floor:** SSBOs are guaranteed only in compute on GLES 3.1
   (`MAX_FRAGMENT_SHADER_STORAGE_BLOCKS` min is 0) — a fragment-path renderer
   may be unable to read the command buffer on the embedded target ADR-0005
   keeps open.
2. **Invocation-group uniformity:** in compute the invocation group is the
   workgroup, so per-tile command walks (and sampler-array indices derived
   from them) are dynamically uniform and legal; in fragment the group is the
   whole draw, making per-tile-derived sampler indexing non-uniform.
3. **Tile = workgroup symmetry:** shared-memory staging of the tile's
   commands, whole-workgroup empty-tile early-out, and room for subgroup
   intrinsics or pass fusion later.

Costs accepted: explicit-LOD texture fetches everywhere (no implicit
derivatives), no ROP sRGB/compression on the offscreen target, weaker
pixel-history debugging in RenderDoc.
