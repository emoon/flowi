# One library, one `Fl`/`fl_` prefix, five source modules

## Status

accepted (supersedes the prefix decision in ADR-0003; fixes the standalone-profile prefix that
ADR-0030 left configurable)

The pre-release code carried three naming regimes at once: the prefix of the host application the
library was extracted from, a library-specific `Em`/`EmUi` pair (ADR-0003), and an `Fl` prefix from
an earlier research prototype. The consolidation to a single prefix is complete.

## Decision

**One library, one prefix, a small set of internal modules.**

- **One prefix everywhere: `Fl` / `fl_`.** Types are `Fl*`, functions are `fl_*`, enum values are
  `Fl<Enum>_*`, macros are `FL_*`. This covers the foundation (arenas, strings, jobs, VFS, platform
  interfaces) as well as the UI toolkit. The `.defs` are prefix-free and api_gen adds the prefix
  (`Naming::new("Fl", "fl")` in `codegen/src/lib.rs`), so the public surface is never hand-renamed.
  ADR-0003's two-tier `Em`/`EmUi` split is dropped: one owner, one prefix.

- **Single library, not an umbrella.** flowi is one cohesive library with clean internal module
  boundaries, not a family of independently-versioned products. The boundaries are kept sharp enough
  to split a module out later if a real consumer ever needs it, but that split is not made now.

- **Public scope is a foundation tier plus the UI toolkit, not a full app framework.** flowi does
  **not** promise to own the platform: per ADR-0001/0011 the host injects the render backend and owns
  windowing and input. The platform backends in the repository are offered as an optional module, never
  as the thing flowi guarantees for every platform.

- **Module structure with provenance-revealing includes.** Source modules form a strict dependency
  DAG; the public headers name their module. "Depends on" counts `#include` edges; an interface
  that one module declares and another implements counts as a dependency of the implementer (see
  the 2026-11-01 amendment):

  | module     | depends on                          | holds                                                     |
  |------------|-------------------------------------|-----------------------------------------------------------|
  | `core`     | (nothing)                           | arenas, `FlString`, jobs, OS layer, math, json, logging   |
  | `platform` | `core`                              | display/input backend interfaces, headless, optional `sdl/` |
  | `vfs`      | `core`                              | virtual filesystem and its driver interface               |
  | `render`   | `core`, `ui` seam headers           | software and GPU render backends (internal)               |
  | `ui`       | `core`, `vfs`, `platform`           | widgets, layout, painter, input state, the render seams   |

  Public includes name their source module: `#include <flowi/core/string.h>`,
  `<flowi/platform/backend_display.h>`, `<flowi/ui/list.h>`. `core` stays platform-free; the SDL
  dependency is a leaf, never a root. The physical layout is ADR-0033.

- **Rust is layered by the same boundaries.** The workspace under `rust/` holds `flowi` (the safe
  façade), `flowi_sys` (generated raw bindings), `flowi_macros`, and the foundation crates
  `flowi_core` / `flowi_core_sys` and `flowi_vfs` / `flowi_vfs_sys`. Rust type names drop the `Fl`
  prefix (ADR-0027).

## Considered Options

- **Umbrella / monorepo of separately-versioned modules (Qt's org-chart).** Rejected for now:
  multiplies versioning, inter-module API-stability, and build-matrix surface for a solo-maintained
  project, with no consumer demanding a module in isolation. The directory discipline gives the
  benefit; the org chart is a tax deferred until needed.

- **Full batteries-included app framework (own the display/input stack publicly).** Rejected:
  contradicts the host-injects-the-backend seam (ADR-0001/0011) and would make flowi liable for
  every platform it names. The value wanted from "like Qt" is *coherent lib + strong foundation*,
  not *owns the platform*.

- **Keep the foundation under a separate prefix.** Rejected: it permanently institutionalizes the
  prefix mash ADR-0030 calls out and leaves the foundation a foreign guest in its own library.

## Consequences

- ADR-0003 is superseded on naming; the `Em`/`EmUi` two-tier split is gone.
- ADR-0030's standalone-profile prefix is `Fl`/`fl_`.
- CONTEXT.md's runtime vocabulary is unaffected; only symbol spellings changed.

## Amendment (2026-11-01): the seams are owned by their consumer

The original table had `ui` depending on `render` and `render` depending only on `core`. The code
has always run the other way. Each boundary follows one rule: the module that consumes a seam
owns the seam's header, and the module that implements it includes that header.

- **ui → render is an interface that ui owns.** ui drives rendering through `FlRenderBackend`
  (`ui/render_backend.h`) and the render IR (`ui/render_ir.h`) and never includes a `render/`
  header. The backends implement those seams, so `render` includes ui's seam headers:
  `render_backend.h`, `render_ir.h`, `font_cache.h`, `vector_image.h`, `image.h`,
  `image_decode.h`, `image_system.h` (load-request types), the software image-cache boundary
  (`image_cache.h`, `image_private.h`), `ui_ids.h`, `image_registry.h` (the 3D viewport exception
  below), and the generated-type umbrella `ui_types.h`. It never
  includes `ui_private.h` or any widget code. The headers stay in `ui`, and no `render/iface/`
  directory is created.
- **The backend reports readiness and ui records it.** `prepare_image` returns whether the image
  was realized, and ui's image system marks the readiness registry (ADR-0025) on the same thread
  the backend prepared on. The exception is the GPU backend's 3D viewport color target, which marks
  and drops the registry entry itself because only the backend knows when it evicts a stale
  target.
- **platform → ui is an input sink that platform owns.** The window backends feed events through
  `platform_input_push_*`, declared in `platform/input_types.h`. They hold only an opaque
  `FlUiCtx*`, and ui implements the sink (`ui/ui_input.c`) along with the per-frame input update
  (`ui_input_update`). platform includes no ui header beyond the generated public
  `flowi/ui/cursor.h`, and `flowi/platform/input.h` itself reaches the generated key types.
- **ui → platform and ui → vfs are plain dependencies.** ui reads the event types and the
  clipboard from `platform`, and loads images and fonts through `vfs`.
