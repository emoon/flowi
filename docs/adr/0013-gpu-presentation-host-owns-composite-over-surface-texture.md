# GPU presentation: host owns the composite over a surface-texture seam; windowed hosts never live in the bundle

## Status

accepted

The seam decision stands. The repository layout it was written against (a published bundle, a consumer-provided
`base`, a monorepo subtree) no longer exists; the accessor now takes the surface and returns a handle struct
(see Decision), the runnable example is `examples/gpu_window/`, and the vtable is `FlRenderBackend`.

## Context

The GPU backend renders the UI into an offscreen RGBA8 target carrying premultiplied
coverage alpha (ADR-0006), and presentation is deliberately host-side and out of the neutral
renderer vtable (ADR-0001). What was unresolved: how a host obtains that target to composite
and present it, and — given flowi is the canonical UI-toolkit repo extracted from the
monorepo (the host's own ADR "ui-toolkit-separate-repo") — *where the windowing host
and any runnable GPU example are allowed to live*.

The trigger was an early GPU-host request that, as written, asked for a single
deliverable spanning three different homes: a production SDL GL `DisplayBackend` (window +
context + display-plane composite + capture), the toolkit-side hook it consumes, and a way to
see the GPU backend run interactively. But:

- `DisplayBackend` is a **base** type; `base` is consumer-provided, fetched into a
  git-ignored `vendor/base/`, and **not committable** from this repo (the UI repo's tracked
  content must stay equal to the bundle so the `git subtree` mapping is 1:1).
- The **published unit is the bundle** — `ui` + `renderer` + `image_loader`. `tests/`,
  `examples/`, `scripts/`, and the standalone CMake are *scaffolding*: the monorepo copies
  them via subtree but never compiles them, and the toolkit boundary check audits only the
  bundle.
- There is no application or windowed host in this repo; the GPU backend's only consumers are
  the two headless test executables.

So the question is the boundary between toolkit-library code, toolkit scaffolding, and the
consumer's host layer — and the mechanism that crosses it.

## Decision

**The host owns the compositor; the bundle exposes only a surface-texture seam; windowed
hosts and the production display backend live outside the bundle.**

- **Surface-texture seam (bundle).** The GPU backend exposes a backend-specific accessor,
  `gpu_renderer_surface_texture(FlSurface* surface)` returning
  `GpuSurfaceTexture { handle; i32 width; i32 height; }` — the offscreen target's handle (a `u32` GL texture
  name; an opaque `void*` `id<MTLTexture>` on Metal) and dimensions, one per window surface. It is a free
  function in `gpu_renderer.h`, **outside** the neutral `FlRenderBackend`
  vtable (presentation stays host-side, ADR-0001), following the `gpu_debug_*`
  pattern in `gpu_debug.h`. On GL the texture crosses as a plain `u32`; **no GL type leaves the backend header.**
- **Host owns compositing and composited capture.** Clear, the display plane (dest rect,
  enable flag, per-plane alpha), the UI layer composited over it, present, and any readback of
  the *composited* result are all host responsibilities. The bundle never draws the display
  plane and ships **no** shared compositor — two hosts have genuinely different needs and a
  display-plane/alpha/capture abstraction in the library is exactly the host-leak ADR-0001
  forbids. The bundle keeps only its existing optional `read_pixels` (UI-texture readback for
  tests).
- **Two hosts consume the seam, neither in the bundle.** (1) The production windowed
  **SDL GL `DisplayBackend`** lives in the consumer/host layer (`base` / the monorepo). (2) A
  standalone **`examples/gpu_window/`** lives in toolkit *scaffolding* — SDL2 directly (window
  + GL context + event loop), depending only on SDL2 + the bundle, guarded behind
  `find_package(SDL2)` like the EGL guard on the GPU path, **dev/hardware-only** (compute
  SSBOs abort on llvmpipe; not a CI gate), with **no display plane** (core-video-under-UI is a
  host concept the toolkit has no source for). The seam itself is CI-covered headlessly in
  `gpu_smoke_test`.

## Considered alternatives

- **Keep flowi windowless; demo the GPU backend only through the embedding host.** Rejected: it
  sacrifices the "reusable across our own projects without dragging the frontend along" goal
  of the extraction (the host's own ADR on the separate-repo split). A toolkit you cannot run standalone is hard to
  develop and harder to trust.
- **A shared GL compositor in the bundle.** Rejected: bakes display-plane / per-plane-alpha /
  capture host policy into the library — the host-leak the boundary exists to prevent — for
  two consumers whose composite needs differ. A little duplicated quad-blit is cheaper than a
  wrong abstraction in the library.
- **Add a present/composite slot to the `FlRenderBackend` vtable.** Rejected: directly violates
  ADR-0001 (presentation is never in the vtable) and would force every backend to model a
  concept only the GPU host needs.
- **Make the windowed example a third test executable / fold it into `gpu_smoke_test`.**
  Rejected: it is not a pass/fail test, the repo fixes the test executable set at two, and a window
  + SDL would break `gpu_smoke_test`'s EGL-surfaceless, CI-headless contract.

## Consequences

- The GPU backend grows one small accessor; the rest of the original scope (the SDL
  GL `DisplayBackend`, display-plane composite, composited capture, real-app wiring) becomes
  separate work in the consumer that *consumes* the seam.
- flowi gains a dev-program tier (`dev/`) alongside `tests/` — committed scaffolding, boundary
  exempt. Future internal runnable demos land here.
- `CONTEXT.md` gains the **Surface Texture** term for the seam.
- The same seam serves multi-window later (ADR-0011): when a surface argument is threaded
  through `render`/`change_resolution`, the accessor takes the surface too — a parameter
  change, not a model change.

### Update: fl_app becomes a third seam consumer

The `fl_app` runner gained a GPU rendering path (`app/app_gpu.c`), so the "two hosts consume
the seam" count above is now **three**: the production display backend, `examples/gpu_window/`,
and `fl_app`'s windowed GPU path. The invariant holds — `app/` is the repo's host/runner layer,
**outside** the published bundle (`ui` + `renderer` + `image_loader`), and the windowed path is
a per-host fullscreen-quad composite with no shared bundle compositor. `fl_app` selects software
(default) or GPU via `FlAppSettings.renderer` (`FlAppRenderer`), and its GPU path also runs the same Surface
Binding headlessly on an EGL-surfaceless context, making it CI-testable in `gpu_smoke_test`.
