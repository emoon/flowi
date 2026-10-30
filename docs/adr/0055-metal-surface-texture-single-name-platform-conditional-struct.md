# Metal Surface Texture: one accessor name, a platform-conditional struct with a neutrally-named handle

## Status

accepted. Implemented: `gpu_renderer.h` carries the platform-conditional `GpuSurfaceTexture` (field `handle`) and, on
Apple, `GpuSurfaceIOSurface`.


## Context

The GPU render backend (ADR-0001, ADR-0005) renders the UI into an offscreen
RGBA8 target carrying premultiplied coverage alpha, and presentation is
deliberately host-side and *outside* the neutral `FlRenderBackend` vtable
(ADR-0001, ADR-0013). A host reaches that target through a backend-specific
accessor, `gpu_renderer_surface_texture(FlSurface*)`, that lives in
`gpu_renderer.h` next to the `gpu_debug_*` calls — not in the vtable — and
returns the offscreen target's handle plus its dimensions. On the GL backend
that handle is a GL texture name, so it crosses the header as a plain `u32` and
**no GL type leaves the backend header** (ADR-0013). The struct today is:

```c
typedef struct GpuSurfaceTexture {
    u32 texture; // GL texture name of the offscreen RGBA8 target
    i32 width;
    i32 height;
} GpuSurfaceTexture;

GpuSurfaceTexture gpu_renderer_surface_texture(FlSurface* surface);
```

ADR-0005 accepted macOS as a non-target for the GL backend (Apple caps OpenGL
at 4.1, no compute) and named a **Metal backend behind the same render-command
IR** as the additive macOS story. That backend has since landed. On Metal the
composited target is an `id<MTLTexture>`, not an integer name — a retained
Objective-C object handle. The open question this ADR settles, ahead of any
code, is: **what shape does `gpu_renderer_surface_texture` take on Metal, given
the handle is a pointer-sized object rather than a `u32` GL name, without
leaking `MTLTexture` into the header and without forcing every host call site to
`#ifdef` per platform?**

The seam has three constraints that shape the answer:

- **No graphics-API type crosses the header** (ADR-0013). `MTLTexture` /
  `id<MTLTexture>` must not appear; the handle must be a neutral spelling.
- **The mental model is identical across backends** (ADR-0013): the backend owns
  the offscreen target, the host samples it and composites/presents. Only the
  handle *type* legitimately differs.
- **This accessor is hand-written, not api_gen-generated.** It is a
  backend-internal seam in `gpu_renderer.h`, not part of any public plugin
  surface produced by api_gen, so it is not layout-frozen plugin ABI in
  ADR-0043's sense.

## Decision

**Keep one accessor name; make the handle field compile-time
platform-conditional and neutrally named.**

The **same function name** `gpu_renderer_surface_texture(FlSurface*)` returns the
**same struct type** `GpuSurfaceTexture` on both backends. The first field is
renamed from `texture` to the neutral `handle` and is selected at compile time:

```c
typedef struct GpuSurfaceTexture {
#if __APPLE__
    void* handle;   // opaque id<MTLTexture>, retained by the backend
#else
    u32 handle;     // GL texture name
#endif
    i32 width;
    i32 height;
} GpuSurfaceTexture;
```

- **Neutral field name.** `handle` (not `texture`) so the name carries no
  backend flavour and a future IOSurface-backed variant could reuse it. On GL it
  is the `u32` GL name exactly as today; on Metal it is a `void*` carrying the
  opaque `id<MTLTexture>`. `MTLTexture` never appears in the header — `void*` is
  the neutral spelling, honouring ADR-0013's "no graphics-API type leaves the
  header."
- **Shared lifetime rule.** Backend-owned on both platforms, valid until the
  next `change_resolution` for that surface — identical to GL today. The host
  never retains or releases the handle; on Metal the backend owns the
  retain/release of the `MTLTexture`.
- **Platform-conditional size, consumed locally.** Because the first field is
  `u32` on GL and `void*` on Metal, `sizeof(GpuSurfaceTexture)` **differs across
  platforms** (`u32` + `i32`×2 = 12 bytes on GL builds; `void*` + `i32`×2 = 16
  bytes on 64-bit Apple — the extra 4 bytes are the wider 8-byte pointer, not
  padding; neither layout has any trailing pad). The struct is a
  value returned and consumed *locally by the same compile unit's SDK target*.
  Host code **MUST NOT `memcpy` the struct across a cross-platform / cross-SDK
  compile-unit boundary**, and must never assume a fixed on-the-wire layout.
- **Not layout-frozen plugin ABI (ADR-0043).** The accessor is hand-written and
  backend-internal — it is not an api_gen-generated public plugin surface — so
  its struct layout is explicitly **not** the downstream `.so` plugin ABI that
  ADR-0043 layout-freezes. The differing `sizeof` across platforms is therefore
  admissible: ADR-0043's freeze does not apply here.

The accessor stays **outside** the neutral `FlRenderBackend` vtable (ADR-0001,
ADR-0013), following the `gpu_debug_*` pattern; the host includes
`gpu_renderer.h` directly. This decision changes only the handle field's type
and name — the seam's placement, ownership model, and lifetime are unchanged
from ADR-0013.

The `gpu_renderer.h` edit (renaming `texture` → `handle` and adding the `#if __APPLE__` split) landed with the
Metal implementation.

## Considered alternatives

- **Two spellings — `gpu_renderer_surface_texture_metal()` beside the GL one.**
  Rejected: forces host code to `#ifdef` per platform at every call site, when
  the concept is one "give me the composited texture" seam. The one-name /
  conditional-field shape keeps a single call site; the divergence is confined
  to one field inside one struct.
- **Opaque `void*` handle on all platforms.** Rejected: removes GL's honest
  `u32` typing and invites retain/release confusion on GL, where the handle is a
  GL name (an integer), not a pointer. The conditional field keeps each backend
  honestly typed.
- **`IOSurfaceRef`-backed texture from day one.** Rejected: sets up cross-process
  plugin hosting (a plugin host compositor sampling the UI from another process)
  that the first in-process host did not need. When a plugin host actually needs
  it, it is added as a **second accessor beside** the existing one — the same
  pattern by which `gpu_debug_readback` sits beside the primary seam — not by
  retrofitting the primary accessor.
- **Inverted control — the host supplies the `MTLTexture` and the backend renders
  into it.** Rejected: couples the renderer to the drawable lifecycle and
  diverges from the GL model, where the backend owns the offscreen target. The
  whole point of the seam is to keep the mental model identical across backends
  (ADR-0013); inverting ownership on Metal alone would break that.

## Consequences

- Metal preserves ADR-0013's mental model verbatim: the backend owns the
  offscreen target, the host composites and presents. Only the handle *type*
  differs (`u32` GL name vs opaque `void*` `MTLTexture`), behind one accessor
  name and one neutral field name.
- The **Surface Texture** term in `CONTEXT.md` is amended: the handle is "a `u32`
  GL name on GL, an opaque `void*` `MTLTexture` handle on Metal — same accessor,
  same lifetime, same neutral field name `handle`," with the caveat that the
  struct's size is platform-conditional and it is consumed locally, never
  `memcpy`'d across SDK boundaries. The rest of the definition (out-of-vtable,
  host-owned compositor, no graphics-API type in the header) is unchanged.
- The `gpu_renderer.h` field rename `texture` → `handle` was a small source change made with the Metal
  implementation; every GL call site uses the neutral name.
- ADR-0013's "three hosts consume this seam" (production display backend,
  `dev/gpu_window/`, `fl_app`'s windowed GPU path) grows by one on the Metal
  side: the Window Backend's Metal presentation mode (ADR-0037, `FlWindowBackendMode_Metal`),
  which presents a `GpuSurfaceTexture` whose `handle` is an `id<MTLTexture>`.
- Not a downstream plugin ABI concern under ADR-0043: the accessor is
  hand-written, not api_gen-generated, so the platform-varying `sizeof` does not
  violate the layout freeze.



## Amendment — the IOSurface variant (out-of-process host)

The "Considered alternatives" above rejected an `IOSurfaceRef`-backed texture
*from day one*, and named the eventual shape: when a plugin host actually needs
cross-process sampling, it is added as a **second accessor beside** the existing
one, "the same pattern by which `gpu_debug_readback` sits beside the primary
seam." That second accessor now exists on Metal:

```c
#if __APPLE__
typedef struct GpuSurfaceIOSurface {
    void* handle; // opaque IOSurfaceRef backing the offscreen RGBA8 target (linear, premultiplied)
    i32 width;
    i32 height;
} GpuSurfaceIOSurface;

GpuSurfaceIOSurface gpu_renderer_surface_iosurface(FlSurface* surface);
#endif
```

- **Additive, not a retrofit.** `gpu_renderer_surface_texture` is unchanged — a
  same-process host keeps receiving the `id<MTLTexture>` handle. The new accessor
  hands back the offscreen target's backing **IOSurface** instead, so a host in a
  *different process* (a plugin whose host compositor lives in another
  process) can open the same pixels into its own `MTLTexture` and sample the UI
  layer cross-process.
- **On-demand upgrade.** The first `gpu_renderer_surface_iosurface` call on a
  surface latches it into IOSurface mode and rebuilds the offscreen target as an
  IOSurface-backed `MTLTexture` at the current resolution. Both accessors then
  view the **same resource** (the texture accessor returns the `id<MTLTexture>`,
  this one returns its backing IOSurfaceRef). A surface the host never asks about
  keeps the default private-storage target, so the common path is byte-for-byte
  as before.
- **Same lifetime, same neutral spelling.** Backend-owned, valid until the next
  `change_resolution` for that surface (which reallocates the IOSurface at the new
  size while keeping the surface latched). The handle crosses as an opaque
  `void*` carrying the retained IOSurfaceRef; **no IOSurface / Metal type leaves
  the header** (ADR-0013), exactly like the primary accessor's `void*`
  `MTLTexture`.
- **Colour-space contract.** The IOSurface pixels are **linear RGBA8 carrying
  premultiplied coverage alpha** — identical to the `id<MTLTexture>` the primary
  accessor exposes (CONTEXT.md's Surface Texture term). The cross-process consumer
  negotiates around that known contract; it is documented in `gpu_renderer.h` at
  the accessor and cross-referenced here.
- **Storage mode.** An IOSurface-backed texture cannot use private storage, so the
  target is Shared on unified-memory devices (Apple Silicon) and Managed on
  discrete ones (Intel). The render pipeline is unaffected — usage still carries
  ShaderWrite + ShaderRead, and readback still blits to a shared buffer.
- **Still not a plugin ABI (ADR-0043).** Like the primary accessor, this is a
  hand-written backend-internal seam, not api_gen-generated; the `#if __APPLE__`
  struct is consumed locally and is not layout-frozen plugin ABI.

## References

- ADR-0001 — Pluggable render backends via a neutral render-command IR
  (presentation stays host-side, outside the vtable).
- ADR-0005 — OpenGL compute for the GPU backend (names the coming Metal backend
  behind the same IR).
- ADR-0011 — GPU backend owns shared resources; surfaces are per-window (the
  accessor takes the per-window `FlSurface`).
- ADR-0013 — GPU presentation: host owns the composite over a surface-texture
  seam (the accessor this ADR extends to Metal).
- ADR-0037 — WindowBackend: a neutral multi-window OS-integration seam (its Metal
  presentation mode becomes a fourth seam consumer).
- ADR-0043 — flowi's generated data-type layouts are a downstream `.so` plugin
  ABI (why this hand-written accessor is *not* layout-frozen).
