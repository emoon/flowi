# OpenGL compute (not Vulkan) for the GPU render backend

## Status

accepted

Partly superseded: a Metal backend now exists behind the same render-command IR (`flowi/src/render/gpu/metal/`,
built on macOS), so macOS is no longer a non-target for the GPU backend. The OpenGL decision itself stands.

## Context

The GPU backend (ADR-0001) follows the Decima UIPainter model: all UI in one
uber-shader pass over a draw-command buffer, with 8×8 tile culling, plus a
glyph-atlas rasterization pass and a tile-classification pass. A whole frame is
~3 dispatches/draws and a present — there is no draw-call submission bottleneck.
First target is the Linux desktop host (SDL2); hosting inside plugin formats on
Windows/macOS comes later.

## Decision

The GPU backend uses **OpenGL, 4.3 core profile as the floor** (compute
shaders, SSBOs, image load/store), with the context created through **SDL2**
(`SDL_WINDOW_OPENGL` + `SDL_GL_CreateContext`) in the host's surface binding.
Feature use stays close to the GLES 3.1 compute subset so the DRM/embedded
target can adopt the backend via GLES later.

- **Vulkan rejected:** its payoff is CPU submission cost across thousands of
  draw calls and explicit threading — irrelevant at ~3 dispatches/frame. It
  would cost ~2k lines of instance/device/swapchain/barrier boilerplate for
  nothing this backend needs.
- **GLFW rejected:** SDL2 is already in tree with working display/input
  backends; GLFW would be a duplicate windowing dependency.
- **macOS accepted as a non-target for this backend:** Apple caps OpenGL at 4.1
  (no compute) and has deprecated it. The macOS plugin story is a separate
  Metal backend behind the same render-command IR — additive later, exactly
  what the IR boundary exists for. The SDF/tile/atlas design ports to Metal
  conceptually 1:1.

## Consequences

- Surface binding for this backend = SDL GL window + context; presentation is
  `SDL_GL_SwapWindow`, kept out of the renderer vtable per ADR-0001.
- A second GPU backend (Metal) is expected eventually; nothing in
  `render/gpu` may leak GL types across the IR boundary.
