# 3D textures are ordinary images; a dedicated-texture flag keeps them out of the atlas

Status: accepted (extends ADR-0060)

## Context

The minimal 3D API (ADR-0060) needs textures for unlit textured triangles — for a lightmapper, an R8
lightmap that is re-uploaded on every rebake. The original feature sketch gave 3D its own
`create_texture(pixels, format)` resource with its own handle type.

flowi already has an image resource that does nearly all of this: `Image::create_from_pixels`
returns a backend-owned handle that is ready immediately, `ImageFormat::Alpha` *is* single-channel
R8, and ADR-0025 already settles pixel ownership and readiness. What it lacks is re-upload.

The consuming tool shows the *same* lightmap in two places at once: a 2D atlas panel and the 3D
viewport, with click cross-highlighting between them. Two handle spaces would mean uploading those
pixels twice and keeping two re-upload paths in sync on every rebake.

One wrinkle blocks a naive reuse: images at or under `GPU_IMAGE_ATLAS_MAX_DIM` (256) are packed
into the shared image atlas, so the handle resolves to a sub-rect of a larger texture. That is
invisible to 2D drawing, which carries the rect in its draw command, but it silently breaks 3D,
where UVs come from the mesh and address the whole texture.

## Decision

**3D textures are `ImageHandle`s.** There is no separate 3D texture resource or handle space; the
3D API's retained set is **meshes only**.

Two additions to the image API support it:

- **`Image::update_pixels(handle, pixels)`** — re-upload into an existing handle, keeping identity
  stable across a re-bake. Generally useful beyond 3D.
- **A dedicated-texture creation path** (`Image::create_dedicated`) — an image created with it always gets its
  own texture and never enters the image atlas. Required for any image sampled by a mesh.

The dedicated property is set **at creation and is explicit**, not inferred from size. An image cannot be pulled
back out of the atlas after the fact, and a size-based rule would make a scene that happens to bake
a 256×256 lightmap render garbage while a 257×257 one works.

## Considered alternatives

- **A separate `Texture3d` handle space.** Cleanly isolates 3D from the atlas, readiness registry
  and the 120-frame dynamic-image eviction. Rejected: duplicate uploads and duplicate lifetimes for
  exactly the case the feature exists to serve, where a texture is shown in 2D and 3D at once.
- **Infer "never atlas" from dimensions.** Rejected as an invisible cliff at 256 px.
- **Pass atlas UV transforms into the 3D shader.** Keeps atlasing available to 3D, at the cost of
  per-draw UV scale/bias plumbing and a bleed problem at chart borders — real complexity to keep an
  optimisation that matters only for textures small enough not to matter.

## Consequences

- `ImageFormat::Alpha` is flowi's name for the R8 lightmap format; "R8" is a shader-side spelling,
  not public API vocabulary.
- A dedicated image costs one of the raster pass's fixed image slots when referenced
  (`GPU_IMAGE_SLOT_COUNT = 11`, first-reference-wins, overflow drops predictably per ADR-0006's
  fixed-capacity contract). Tools with many simultaneous large images and viewports share that
  budget.
- `update_pixels` must respect the same backend-owned-pixels contract as ADR-0025: the caller may
  free its buffer after the call returns.
