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
  DAG; the public headers name their module:

  | module     | depends on       | holds                                                     |
  |------------|------------------|-----------------------------------------------------------|
  | `core`     | (nothing)        | arenas, `FlString`, jobs, OS layer, math, json, logging   |
  | `platform` | `core`           | display/input backend interfaces, headless, optional `sdl/` |
  | `vfs`      | `core`           | virtual filesystem and its driver interface               |
  | `render`   | `core`           | software and GPU render backends (internal)               |
  | `ui`       | `core`, `render` | widgets, layout, painter (`platform` only at the app edge) |

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
- CONTEXT.md's runtime vocabulary is unaffected; only symbol spellings changed. The "Backend" family
  (`DisplayBackend`/`InputBackend`) keeps its meaning and lives under `flowi/platform`.
