# Dynamic images are sampled interleaved, never deinterleaved to planar

## Status

accepted

The ABI break and the backend-private SW convert are implemented; in-place interleaved SW sampling remains
deferred. The Context below describes the code before the break.

## Context

Dynamic images let a plugin plot pixels into a buffer every frame (visualizers:
oscilloscope, tracker view, spectrum). Previously `FlDynamicImage` embedded a planar
`render_image` and `dynamic_image_update()` deinterleaved RGBA → planar each frame for the
software renderer's SIMD path; the core reached into that representation directly
(`ui.c` and `painter.c` cast `&image->render_image` to `ImageData*`).

The planar deinterleave is an *amortization* optimization: a **static** image converts once
(on a job thread, in the decode sink) and is then sampled for hundreds of frames, so the
convert cost is amortized to ~zero per frame. A **dynamic** image's pixels change every
frame, so the buffer would be converted every frame and sampled once — the amortization is
absent and the convert is pure per-frame overhead (CPU + a second full-size buffer).

We are introducing pluggable [[Render Backend]]s behind a neutral IR (ADR-0001). The public
`FlDynamicImage` must not embed a software-renderer representation.

## Decision

- The user-facing dynamic-image format is a single **backend-agnostic interleaved RGBA**
  buffer (plus the alpha-only variant), zero-copy. `FlDynamicImage` keeps
  `user_buffer` + dimensions + `is_alpha_only` and **drops the planar `render_image`** — a
  clean ABI break, consistent with ADR-0001's image break.
- Each backend **owns** realization of that buffer: **SW** keeps the planar form
  backend-private (today via its existing per-frame RGBA→planar convert, now writing a
  backend-owned buffer instead of the public struct); **GPU uploads** the interleaved buffer
  to a texture/atlas each frame (interleaved RGBA is GPU-native).
- **Sampling the interleaved buffer in place** — skipping the SW convert entirely — is the
  intended end state, but the SW renderer today samples *only* planar (`sr_render_image.c`
  reads separate R/G/B/A planes) and force-converts interleaved input
  (`sr_ir_to_render_command.c`). In-place interleaved sampling therefore needs
  a **new** SW sampler (the full 1/3/4-channel × blend × nearest/bilinear matrix, with
  bilinear edge-padding parity) and is a **deferred optimization**, not part of the ABI break.
- Deinterleaved/planar [[Representation]] stays a software-private optimization for **static**
  images only (converted once in the async decode sink).
- **Scaled** dynamic images (rare — visualizers render at native size) fall back to
  convert-on-demand rather than gaining a dedicated interleaved scaled path.

## Considered alternatives

- **Keep the per-frame RGBA → planar convert as the *permanent* SW path.** Rejected as the
  end state: pure per-frame CPU + memory overhead with no amortization; cost grows with image
  size. (It is retained as the *interim* SW realization until the in-place interleaved sampler
  lands — the ABI break does not depend on the sampler.)
- **Unify dynamic images into `FlImageHandle` + representation.** Rejected for now: heavier
  change to the "plot pixels into a buffer" ergonomics, and dynamic images are a demoted
  fallback (the optional polyline/arc/bar primitives are the preferred visualization path),
  so the churn isn't worth it.
- **Build an interleaved *scaled* sampler now.** Deferred: native-size rendering dominates;
  convert-on-demand covers the rare scaled case.

## Consequences

- The ABI/ownership break is immediate and pixel-identical (SW keeps its current convert, now
  backend-private). The CPU/memory win — dropping the per-frame deinterleave and the planar
  mirror — is realized only when the deferred in-place interleaved sampler lands.
- `ui.c` / `painter.c` stop referencing `render_image`; dynamic images flow through the IR
  as a backend-realized resource.
- The GPU backend gets dynamic images "for free" via per-frame upload.
- A scaled dynamic image pays a convert; accepted given its rarity.
