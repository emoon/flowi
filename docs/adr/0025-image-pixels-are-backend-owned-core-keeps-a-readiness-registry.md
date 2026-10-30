# Image pixels are backend-owned; the core keeps a readiness registry

The image-pixel store (a fixed 512 MB TLSF pool holding decoded, padded, deinterleaved
pixels) was allocated unconditionally by the UI core in `ui_create`, regardless of which
[[#Render Backend]] the host injected. Only the SW backend ever writes to it — its
`prepare_image` realizes pixels into the pool and its render path samples them. The GPU
backend uploads decoded images straight into its atlas / slot textures and never touches the
pool, so under the GPU backend the entire pool was allocated and stayed empty for the life of
the context.

The waste was the visible symptom; the cause was an ownership inversion. Per ADR-0001 a
backend owns resource *realization* — the prepared, backend-native form of a resource behind a
handle (CONTEXT.md *Representation*). The pool is exactly that: SW-native sampled pixels. It
sat in the core only because, historically, the core's image API *was* the SW pixel store — one
object did double duty as (a) the handle → readiness/dimensions lookup the core needs and
(b) the SW backend's pixel cache (plus its eviction and on-demand reload). The core's
`image_get_data` returned a full pixel struct, but every backend-agnostic caller (painter,
the image widget, the public/Rust seam) read only `status` + logical `width`/`height` — never a
pixel. The public ABI already projected pixels away (CONTEXT.md *Resource Handle*).

This double duty also hid a latent bug. The image widget gates emission on
`image readiness`, and readiness was inferred from "are there CPU pixels in the cache?" — a
SW-only proxy. Under the GPU backend the pool is never populated, so that gate never opens:
`ui_image()` of a static image emits nothing and the GPU's translate-time handle resolver never
even sees the handle. (GPU image *pixel* tests passed only because they hand-build the render
IR with the handle, bypassing the widget readiness gate.)

The decision is to split the one object along the ownership line:

- **The realized-pixel store moves into the SW backend.** The TLSF pool, the
  `IMAGE_CACHE_MEMORY_SIZE_MB` budget, eviction, and the evicted → on-demand-reload machinery
  are created with the SW backend (in its constructor, torn down in its `destroy`) and reached
  only by SW code — its `prepare_image` and its render path. The GPU backend keeps its
  atlas / slots as before. Neither store is allocated by the core, so an
  image-pixel pool exists only when the SW backend does.

- **The core keeps a small backend-agnostic Image Registry** (handle → `{status, width,
  height}`), created with the app-level shared tier
  (`ui_shared_create`). It holds no pixels. It is the single source the public
  image query, `image_is_ready`, the painter, and the layout widget read. The internal
  `image_get_data` collapses to this metadata — matching the public ABI, which already returned
  only status + dimensions.

- **Ready means *realized*, signaled per-backend.** Each backend calls one backend-agnostic
  core ack at the end of its `prepare_image`, carrying the dimensions it already has from the
  decode result. An image is ready when the *active* backend has realized it (SW pixels in the
  pool, GPU texels in the atlas), not when its bytes were merely decoded. This preserves the
  existing SW semantic (it always marked ready *after* storing pixels) and is what makes
  `ui_image()` work under the GPU backend — the missing realization signal that the readiness
  gate needs.

- **Pool pressure stays SW-internal.** SW eviction and reload are invisible to the core: the
  registry says "ready" (dimensions known) for a once-realized image, and a transient SW pool
  miss self-heals — the SW render path drops that frame's command and re-requests the load
  through the normal async load API (the existing handle-reuse reload), drawing it next frame.
  The core never sees "ready but not currently resident."

## Considered Options

- **Narrow waste-fix: move only the pool allocation out of `ui_create`, leave everything
  else.** Rejected as incoherent. The core's readiness/dimension lookups read the same object;
  remove the pool from the core and those callers have nothing to resolve a handle against under
  the GPU backend. The metadata registry is required either way, so the "just stop allocating
  it" version cannot stand on its own.

- **Lazily / conditionally allocate the pool (e.g. only when the SW backend is active, but keep
  it core-side).** Rejected: it fixes the byte count while keeping the ownership inversion — the
  core would still branch on which backend it built, which is exactly the compile-time-free,
  enum-free core boundary ADR-0001 forbids. Ownership, not allocation timing, is the defect.

- **Extend the backend interface so the core can reach into the SW pool for pixels.** Rejected:
  it re-cements the coupling the split removes. The core never needs pixels — only readiness and
  dimensions — and the only pixel consumers (SW element and background render paths) are inside
  the SW backend, where they reach the pool directly. Exposing pool pixels across the seam would
  add a SW-shaped method the GPU backend cannot honor.

- **Readiness = decoded (backend-neutral), instead of realized.** Rejected: the GPU backend
  decodes on a job thread but defers the texture upload to the begin-frame pump on the render
  thread. Marking ready at decode would let the widget emit an IR element one frame before the
  atlas has the texels, so the translate-time resolver misses and nothing draws that frame.
  *Realized* is the readiness the drawing path actually requires.

## Consequences

- An image-pixel pool is allocated only when the SW backend is present. A GPU-only context
  carries the small registry and no pixel pool.
- `ui_image()` of a static image works under the GPU backend for the first time; the readiness
  gate now opens on a real cross-backend realization signal rather than a SW-only pixel-presence
  proxy.
- The internal `image_get_data` returns metadata only; the pixel-returning lookup becomes
  SW-internal. Both SW pixel readers (element path and background crossfade/direct-copy path)
  read the SW pool directly rather than through the core API.
- The SW backend gains ownership of eviction and reload; the core image path no longer knows
  eviction exists. "Ready" means "was realized," so a SW pool miss is a transient,
  self-healing internal event, never a core-visible state.
- The core exposes one backend-agnostic realization ack, `image_registry_mark_realized` (handle +
  dimensions); both backends call it at the end of `prepare_image` and the registry consumes it.
