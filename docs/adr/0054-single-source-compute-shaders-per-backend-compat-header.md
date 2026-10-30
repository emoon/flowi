# Compute shaders stay one file per shader, bridged by a per-backend compat header

## Status

accepted. Implemented: the Metal backend (`flowi/src/render/gpu/metal/`) compiles these shader bodies through
`shader_compat_metal.h` and the Metal arm of the preamble hook in `gpu_shaders.c`. The shared vocabulary has since grown
beyond the list below (`WRITE_BUFFER`, `IMAGE2D_WRITE`, `LOCAL_SIZE_2D`, `UNIFORM`, `SAMPLER2D`), and the 3D mesh
shaders (`mesh3d_vert.glsl`, `mesh3d_frag.glsl`) are separate (ADR-0060).


## Context

The GPU backend (ADR-0001, ADR-0005) runs its whole frame as a few compute
dispatches: classification, raster, glyph coverage, and color-SVG coverage. The
shader corpus lives as one file per shader under
`flowi/src/render/gpu/shaders/*.glsl` — `classify.glsl`, `raster.glsl`,
`glyph.glsl`, `color_svg.glsl` plus shared snippets (`coverage_kernel.glsl`,
`drawcmd.glsl`, `tile_defs.glsl`, `kind_defs.glsl`, `segment.glsl`). They are
GL 4.3-core compute today: `layout(local_size_*) in`, `layout(std430, binding=N)
buffer`, `shared`/`barrier()`, `imageStore`, `gl_GlobalInvocationID`. The build
embeds each file as a NUL-terminated byte array and `gpu_shaders.c` assembles a
per-program source-string list — `#version 430\n`, a generated `#define`
preamble of host constants, the shared snippets, then the body — so the `.glsl`
files themselves stay preamble-free.

ADR-0005 accepted macOS as a non-target for the GL backend (Apple caps OpenGL at
4.1, no compute) and named a **Metal backend behind the same render-command IR**
as the additive macOS story, noting the SDF/tile/atlas design "ports to Metal
conceptually 1:1." That backend has since landed. The open question this ADR settles,
ahead of any code, is: **how does the shader source survive two dialects
(GLSL and Metal Shading Language) without the corpus forking into two
hand-maintained copies?**

The two dialects share almost all of a compute shader's *body* — arithmetic,
control flow, the Wallace coverage math, the SDF evaluators — but differ on a
few structural points: scalar/vector type spellings (`vec4` vs `float4`,
`mat4` vs `float4x4`, `ivec2` vs `int2`), how resources and threadgroup memory
are declared, how sync and atomics are spelled, and — the hard one — how a
kernel names its entry point and *binds its resources*. GLSL declares buffers
and images at global scope with `binding=N`; MSL passes every buffer/image as a
function argument decorated with `[[buffer(N)]]` / `[[texture(N)]]` /
`[[thread_position_in_grid]]`. That entry-point-and-binding delta is the one
part that does not reduce to a token swap.

## Decision

The compute shaders stay **one file per shader**. Each shader file selects a
per-backend **compat header** at the top — `shader_compat_glsl.h` on GL 4.3-core
(Linux/Windows), `shader_compat_metal.h` on Metal (macOS) — and both headers
define the *same shared vocabulary*, so the shader **body reads the same across
backends**.

**Shared vocabulary (the canonical list).** Both compat headers define exactly
these names; the shader body uses only them, never a backend-native spelling:

- **Type aliases** — `float2`/`float4`, `int2`, `uint2`/`uint3`/`uint4`, spelled
  the MSL way in shader bodies and aliased on GLSL to the native spelling
  (`float4`→`vec4`, `int2`→`ivec2`, `uint2`→`uvec2`, …; identity on Metal).
- **`BUFFER(block, T, name, N)`** — an SSBO / device buffer of `T` at binding `N`. A
  shared vocabulary *name*, but the one whose expansion lives in the
  per-backend signature seam below (global-scope declaration on GLSL, function
  argument on MSL).
- **`SHARED(T, arr, N)`** — threadgroup / workgroup-shared array of `N` `T`
  (GLSL `shared T arr[N]`; MSL `threadgroup`), e.g. today's
  `shared uint sh_idx[TILE_CAPACITY]` in `raster.glsl`.
- **`WRITE_IMAGE(img, coord, val)`** — a write-only image store (GLSL
  `imageStore`; MSL `img.write`), e.g. today's `imageStore(target, ...)`.
- **`BARRIER()`** — a threadgroup execution+memory barrier (GLSL `barrier()`;
  MSL `threadgroup_barrier(mem_flags::mem_threadgroup)`), e.g. the staging
  barrier in `raster.glsl`.
- **`KERNEL(name)`** — the entry-point marker (see the acknowledged delta
  below).

**The entry-point + resource-binding site is per-backend and un-bridged.** This
is the one delta a macro cannot cleanly hide. In MSL a kernel takes every buffer
and image as a function argument carrying `[[buffer(N)]]` / `[[texture(N)]]`,
and the thread index as a `[[thread_position_in_grid]]` argument; in GLSL the
same resources are global-scope declarations and the index is `gl_*`. `KERNEL`
and the `BUFFER`/image declarations expand differently enough on the two
backends that the *signature region* of each shader is genuinely
backend-shaped, not a token swap. This ADR documents that as **intentional**:
the compat header hides the vocabulary of the body, and the entry-point +
binding preamble is accepted as the small, explicit per-backend seam rather than
forced through a noisy conditional. The shared curve/SDF math below the
signature stays identical.

**The per-backend preamble hook owns the language dialect.** `gpu_shaders.c`'s
source-string assembler grows a per-backend preamble hook that emits the dialect
line: `#version 430\n` on GL (today's `k_version`), and
`#include <metal_stdlib>\nusing namespace metal;\n` on Metal. The generated
host-constant `#define` preamble (`TILE_SIZE`, `TILE_CAPACITY`, atlas sizes, …)
stays as-is; both dialects consume plain `#define`s. **Shader files stay
preamble-free** — the dialect line lives in the assembler, never in a `.glsl`
file — preserving today's rule that the shader files carry no `#version`.

**New files.** `flowi/src/render/gpu/shaders/shader_compat_glsl.h` and
`shader_compat_metal.h`. The existing shader files are rewritten to use the
shared vocabulary in their bodies; the GL backend keeps compiling and running
identically (the GLSL header aliases to today's spellings). A future third
backend adds a third compat header, not a third copy of every shader.

## Considered alternatives

- **SPIR-V cross-compile (glslang + spirv-cross).** Author in GLSL, compile to
  SPIR-V, cross-compile to MSL. Rejected: two build-time dependencies, and the
  translation is imperfect exactly on the compute idioms this corpus leans on —
  workgroup-shared memory, atomics on SSBOs, image writes. Overkill for a
  handful of shaders.
- **Slang or WGSL as the authoring language.** Rejected: heavy new toolchains
  for a small corpus; WGSL in particular does not cover the GL 4.3 compute
  feature set today, so it would regress the existing, working backend.
- **Two independent files per shader, one GLSL and one MSL, kept in sync by
  hand.** Rejected: the shaders evolve (the raster SDF grows kinds, the coverage
  kernel is tuned), and two hand-maintained copies drift silently — precisely
  the failure the single-source rule exists to prevent.
- **Pure `#ifdef GL` / `#ifdef METAL` bridging inside one file, no compat
  header.** Rejected: the entry-point + resource-binding delta would produce a
  noisy `#ifdef` cascade around every kernel signature, burying the shared body.
  The compat header confines the divergence to two small header files and one
  acknowledged signature seam.

## Consequences

- New files `shader_compat_glsl.h` and `shader_compat_metal.h`; the existing
  `.glsl` files are rewritten to the shared vocabulary. The GL backend continues
  to work identically — the GLSL header aliases every vocabulary name to today's
  spelling, so this is a source-refactor with no behavioral change.
- The shader **body** is single-source across backends; the entry-point +
  resource-binding preamble is a small, documented per-backend seam, not silent
  drift. Reviewers know which region is expected to differ.
- The dialect line lives in one place (the assembler's preamble hook), so the
  `.glsl` files stay preamble-free and the host owns the `#version` / metal_stdlib
  choice — the same discipline that already keeps C constants out of the shader
  files.
- A third backend adds one compat header and one preamble branch, not a full
  re-copy of the corpus.

### Explicitly deferred (not in the first Metal landing)

- **MoltenVK / Vulkan-on-Mac portability layer.** Someone will ask. Rejected for
  now: it adds a third build path the Metal backend does not need — the point of
  the IR boundary (ADR-0001) is a native Metal backend, not another translation
  layer.
- **Shader hot-reload on macOS.** The GL dev loop can re-`glShaderSource` and
  recompile in place; the equivalent Metal iteration loop is a UX regression to
  reach parity on. Deferred; it does not block a first landing.

This ADR was written ahead of the Metal backend (ADR-0005) so the shader-source strategy could be reviewed in
isolation; both compat headers now exist under `flowi/src/render/gpu/shaders/`.
