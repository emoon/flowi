# Foundation-service vtables are generated in flowi, beside the host's composed plugin vtable

## Status

Superseded by ADR-0058. Originally accepted as a carve-out beside ADR-0032 (building on ADR-0030
single-source and ADR-0035 "api_gen owns the public surface"). flowi now ships as a shared library
and the foundation services are plain exported `fl_*` symbols; api_gen emits no service vtable
struct, `g_fl_*` global, `*_get_public_api` adapter or `fl_init_*_api` glue. The service `.def`s
still carry `#[attributes(service)]`, but their headers are plain exported-symbol headers in
per-service dirs (`<flowi/arena/arena.h>`, `<flowi/string/string.h>`), not `flowi/service/`. The
body below is the original decision, kept for its rationale.

## Context

ADR-0032 moved the **composed** UI plugin vtable (`FlUiAPI`/`g_fl_ui`, ~100 slots) out of flowi:
flowi exports a `direct` `fl_*` API, the embedding host owns and generates the plugin
vtable, and "`plugin_vtable` is the only place a vtable is generated." That ADR is about the host's
*application* plugin ABI.

The foundation services — `arena`, `string`, `log` — are a different animal. The C++ cores
consume them across the `dlopen` boundary (`FlArena* arena` into every core's `create()`,
`fl_arena_alloc_zero(...)`, …) and **cannot** include flowi's internal `<core/arena.h>`, because its
`_Atomic` / `<stdatomic.h>` body collides with C++ `<atomic>`. flowi must therefore ship a
*clean public* service surface for them. And a `dlopen`'d plugin reaching a service through an
injected table is exactly the dynamic boundary ADR-0032 itself says *does* warrant a vtable —
consistent with the ~7 per-service vtables the host already injects (memory, path, …). These
services are flowi-core's **own** exports, not the host's app surface, so the host has no business
owning their tables.

## Decision

Foundation-service vtables are **generated in flowi and flowi-owned** — a distinct category *beside*
ADR-0032's composed plugin vtable, **not** a reversal of it.

- An `api` block marked **`#[attributes(service)]`** emits the `plugin_vtable` shape — the `FlFooApi`
  struct, the `g_fl_foo` module-local global, the convenience macros, the `fl_foo_get_public_api()`
  adapter, and the `fl_init_foo_api` glue — into **`flowi/include/flowi/service/`**, with the Rust
  façade gated off. One marker, three consequences (emit style + Rust-off + output dir).
- The full convenience-macro surface is **derived from declarative slot attributes** (`[alloc]`,
  `[loc]`, `[scope_begin]`/`[scope_end]`), not hand-written and not smuggled verbatim into the IDL.
  The `[cmacro]` escape hatch is **removed**; irreducible code (log's `va_list` forwarder, string's
  `S()` literals) lives in a hand-written seam fragment the generated header `#include`s.
- ADR-0032 stands untouched for the **composed** UI/VFS app plugin vtable (still host-owned,
  host-generated). flowi-internal callers still invoke the core bodies directly, so 0032's "no
  internal indirection" core principle holds.

Two terms, kept distinct: a **composed plugin vtable** is the host's ~100-slot app ABI (the host's flat UI API struct); a
**service-API vtable** is one flowi-core service a plugin consumes (`g_fl_arena`). The host service
registry still *injects* the table at plugin init; flowi only *generates* the table + adapter.

## Considered Options

- **Host generates the service vtable** against flowi's `direct` `fl_arena_*` (strict ADR-0032).
  Rejected: splits arena's public header across two repos, fights the "arena lives in flowi"
  migration, and flowi must expose the core bodies regardless.
- **No vtable — cores call `fl_arena_*` directly.** Rejected: cores are `dlopen`'d (`RTLD_LOCAL`);
  that dynamic boundary is where ADR-0032 itself says a vtable is warranted, and direct linkage would
  be inconsistent with every other injected service.

## Consequences

- ADR-0032's "`plugin_vtable` is the only place a vtable is generated [in the host]" is refined:
  foundation-service vtables are *also* generated — in flowi — under this carve-out.
- api_gen grows a `service` profile (the `#[attributes(service)]` marker) plus the slot attributes
  that derive the macro surface; `[cmacro]` is retired.
- Service headers physically separate from the per-subsystem call-API headers (`service/` vs `core/`,
  `ui/`, …), so "wire-up-via-vtable" reads differently from "call these `fl_*`" at a glance.
