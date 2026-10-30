# The 3D viewport uses the hardware raster pipeline; the GPU backend is no longer compute-only

Status: accepted

## Context

flowi is gaining a minimal generic 3D API (retained meshes, per-viewport view-projection matrix,
depth-tested unlit triangle and line draws, a dockable `Viewport3d` element rendering into an
offscreen target the UI samples as an image). The driving consumer is a lightmapper
tool, which needs to show lightmapped scene geometry, voxel boxes, light gizmos and a camera path.

The GPU backend today has **no raster pipeline at all**. ADR-0005 put it on OpenGL 4.3 compute,
ADR-0006 made classification and raster a compute-shader tile rasterizer into an offscreen RGBA8
target, and every shader under `flowi/src/render/gpu/shaders/` is a compute shader. There is no
`glDrawElements`, no vertex/fragment stage, no depth attachment, and ADR-0054's single-sourcing
vocabulary (`BUFFER`, `SHARED`, `WRITE_IMAGE`, `BARRIER`) is a compute-shader vocabulary.

So 3D is new machinery either way, and the question is which machinery.

## Decision

**The 3D pass uses the hardware raster pipeline.** Per viewport: a color + depth FBO, vertex and
index buffers for retained meshes, a vertex/fragment shader pair, hardware depth test. The pass runs
before the UI raster pass; its color target is then sampled by the UI as an ordinary image.

**The two unlit shaders are hand-written per backend** — GLSL now, MSL when the Metal arm lands.
ADR-0054's compat-header single-sourcing is *not* extended to cover vertex/fragment stages. The
shaders are on the order of thirty lines each; a cross-backend abstraction for them would cost more
than the duplication it removes.

Metal parity is a separate slice. GL landed first and the Metal backend still has no mesh or 3D viewport seam;
the public API is
platform-neutral so Metal is a fill-in-the-blanks job rather than an API revision.

## Considered alternatives

- **A compute triangle rasterizer reusing the existing tile machinery.** Attractive on consistency
  grounds: one pipeline flavor, ADR-0054 applies unchanged, no FBO or depth-attachment machinery to
  add. Rejected because it means hand-writing depth-buffered triangle rasterization with atomic
  depth resolution — precisely the work the hardware does for free — and it makes line rendering
  and any future MSAA a permanent in-house problem.
- **3D as a new tier of neutral render commands.** Most consistent with flowi's one-seam
  philosophy and visible to the ADR-0047 IR inspector, but it pushes matrices, depth and vertex
  buffers into an IR that ADR-0006 defines as a 2D SDF tile representation, and forces the software
  backend to carry a tier it can never implement.
- **CPU-side 3D in the consuming tool, drawn through `dynamic_image`.** Viable for a lightmapper
  specifically (it already owns a CPU ray-tracer), but it is not a flowi feature, and every future
  tool wanting a 3D view would rebuild it.

## Consequences

- The GPU backend gains a second pipeline flavor: VAO/VBO state, depth attachments, and a
  vertex/fragment shader compile path alongside the compute programs. The claim "the GPU backend is
  compute-only" (ADR-0005/0006 era) no longer holds and should not be relied on.
- Per ADR-0011's tiering, retained meshes are **backend-shared** resources; a viewport's color and
  depth targets are **per-surface-like** — owned by the `Viewport3d` instance, sized to its layout
  rect, and recreated on resize. Meshes are shared across viewports.
- Layout runs during the frame, so a viewport reports its **previous-frame** pixel size (the
  ADR-0023 pull-by-id pattern); the consumer's projection matrix is one frame stale across a resize.
- No MSAA initially, per the feature's agreed scope. Adding it later is an FBO attachment change,
  not an architecture change — which is part of why the hardware path was chosen.
- The software backend renders a placeholder for `Viewport3d`; a tool that needs 3D requires the
  GPU backend.
- A viewport's color target is an ordinary image, so it spends the same budget one does. Concretely
  it holds one entry in the backend's slot table (`GPU_IMAGE_SLOT_TABLE_CAP`, 64 live large/dynamic
  textures) for as long as the viewport lives, and one of the raster pass's per-frame sampler slots
  (`GPU_IMAGE_SLOT_COUNT`, 11) on each frame the UI actually draws it. **That per-frame ceiling is
  reachable in practice**: the core allows 8 simultaneous viewports, so a layout showing all of them
  leaves three slots for every large or dynamic 2D image on screen at once. Past eleven, ADR-0006's
  fixed-capacity contract applies unchanged — the slot-needing commands beyond the eleventh drop in
  stream order and the backend counts them in `overflow`, never rendering something undefined. A tool
  that wants many views plus many large images should dock fewer viewports at a time rather than
  expect the twelfth image to draw.
