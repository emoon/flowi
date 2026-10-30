# WindowBackend: a neutral multi-window OS-integration seam (absorbs input + present)

## Status

accepted (implemented; the seam is the internal `FlWindowBackend` in `flowi/src/platform/window_backend.h`)

## Context

Detachable top-level windows are a hard requirement (a synth/instrument editor live
alongside a main window). Windowing is the last place the toolkit still hardwires a concrete
library: `app/app_gpu.c` created the SDL-GL window directly, and `app/app.c` called
`backend_sdl_display_create` / `backend_sdl_input_create` by name. Everything else the core
touches is an injected neutral seam (`FlRenderBackend`, VFS Driver).

The existing `FlDisplayBackend` / `FlInputBackend` are **not** the right thing to extend:
`FlDisplayBackend` carries video-plane compositing (`upload_display`, `set_display_rect`,
`set_display_alpha`), `backend.c` holds a process-global *single* display, and DRM is a
fullscreen-kiosk framebuffer. That is a *display plane* for the embedded/host path, not a
desktop window — folding multi-window desktop semantics into it would pollute it.

## Decision

Introduce a new neutral injected seam, **`FlWindowBackend`**, parallel to `FlRenderBackend`:
one object the host constructs and the runner drives, so the toolkit names no concrete
windowing library.

- **One backend owns the OS connection and creates per-window objects** (`create_window` →
  an opaque OS-window handle), mirroring how `FlRenderBackend` owns shared resources and
  creates `FlSurface`s.
- It is the **single OS-integration seam**: window lifecycle, the event pump (demuxed per
  window by OS window id), keyboard/mouse/cursor/relative-mouse/warp, per-window drawable
  size + content scale, and **presentation** (composite a `Surface Texture` over the window
  and swap).
- Each host-level `Window` pairs one `FlWindowBackend` window + one `FlRenderBackend` `FlSurface`
  + one per-window `FlUiCtx`.
- SDL is one implementation (`sdl_window_backend_create`, with GL, Metal and software
  presentation modes); a **headless** implementation (`headless_window_backend_create`) makes
  multi-window CI-testable. `FlDisplayBackend` / `FlInputBackend` survive only for the
  embedded/host fullscreen path.

Input lives in this seam because input is intrinsically a property of a window (true even
for a future console window), and demuxing OS events by window id needs the window backend
anyway.

## Considered alternatives

- **Extend `FlDisplayBackend` to multi-window.** Rejected: it is the embedded single-fullscreen
  video-plane abstraction; multi-window desktop semantics do not belong there.
- **Keep `FlInputBackend` separate from windowing.** Rejected: events are per-window off one OS
  queue, and cursor/relative-mode/warp are per-window OS calls — splitting them re-introduces
  a demux-coordination problem across two seams.
- **Keep SDL calls in `app`/`app_gpu`.** Rejected: binds the toolkit to SDL and blocks a
  headless multi-window test path.

## Consequences

- Presentation moves out of the bespoke `app_gpu.c` compositor into the `FlWindowBackend`
  seam. This **refines ADR-0013** ("host owns the GPU compositor"): the `FlWindowBackend` *is*
  the host-side window owner, so the clear → sample-surface-texture → fullscreen-quad → swap
  lives in its GPU implementation. Presentation is still excluded from `RenderInterface`.
- The surface texture crosses as the plain `u32` the "Surface Texture" term already
  specifies — no graphics-API type leaves the backend (on Metal the handle is an opaque `void*`;
  ADR-0055).
- The single shared GL context drives N window surfaces (ADR-0011); presenting to window *i*
  makes that window's context current, samples the shared surface texture, and swaps.
