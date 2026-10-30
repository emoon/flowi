# The render-IR debug inspector belongs in flowi; the IR stays internal

## Status

accepted

## Context

flowi captures a per-window, introspection-only snapshot of its last-frame render commands as
`UiRenderCommand` (`flowi/src/ui/ui_private.h`, behind `#ifdef FLOWI_UI_INSPECTOR`; the build defines it, see Decision): a Clay-independent struct of ~15
fields (id, z-index, type, bounds, name, colors, corner radius, text, font id/size, border color)
exposed through two narrow accessors — `ui_get_render_command_count()` / `ui_get_render_command()`.
It exists for test/MCP introspection and is rebuilt from Clay's `last_render_commands` every frame.

A downstream debug webserver (~744 lines in another repo) reads ~the entire `UiRenderCommand` IR to
serialize the UI tree over HTTP and to locate element centers for click-automation. To do that
across a repo boundary, it would need `UiRenderCommand` promoted to public, frozen-layout ABI.

That is exactly the hazard ADR-0043 warns against: any IDL/layout type a downstream binds across its
`.so` plugin boundary becomes a layout-frozen ABI, so a routine internal edit (reordering a field,
widening an enum, changing a bitfield split) silently breaks already-built downstream binaries.
`UiRenderCommand` is *churny by nature* — it tracks whatever Clay's render command shape happens to
be — so freezing it is the worst kind of type to freeze. It is also introspection plumbing, not a
user-facing widget surface, which makes the same reasoning apply as for other engine/host
plumbing: it gets re-homed to the side of the boundary that owns it, rather than leaking the UI
core's internals. The "substrate-not-views" principle points the same way: flowi owns the
substrate that reads its own IR; transport/views (HTTP) stay app-side.

## Decision

Re-home the **inspector** into flowi as an optional, compile-gated module that reads the internal IR
*inside* flowi, instead of exporting the IR. Concretely:

- **In flowi (optional debug module):** a narrow inspector seam over the existing `UiRenderCommand`
  capture (`flowi/include/flowi/ui/inspector.h`, `flowi/src/ui/ui_inspector.c`) — (1)
  `fl_inspector_snapshot` serializes the current UI tree to a stable, caller-owned JSON text buffer,
  and (2) `fl_inspector_hit_test_id` / `fl_inspector_hit_test_name` return an element's center
  `(x, y)`, with an enumerate form (`fl_inspector_element_count` / `fl_inspector_element_get`)
  yielding `{ id, name, bounds }`, for click-automation. The format-churn that must track IR changes lives
  here, behind the boundary.
- **Stays app-side:** the HTTP server. flowi takes on no socket/HTTP dependency; the downstream
  webserver becomes a thin transport+routing shim over the flowi inspector seam.
- **`UiRenderCommand` stays internal and unfrozen.** Only the serialized snapshot buffer and a small
  flat result type (`FlInspectorHit { id, name, x, y }`) cross the public boundary — neither is
  the IR, so neither becomes a frozen-layout ABI under ADR-0043.
- **Gate:** the module's code is guarded by `#ifdef FLOWI_UI_INSPECTOR` (`ui_inspector.c`, `ui_render_capture.c`,
  the capture fields in `ui_private.h`, `inspector.h`). The build defines `FLOWI_UI_INSPECTOR=1` unconditionally
  on the `flowi_objects` target (`CMakeLists.txt`), so the inspector is always compiled in, in release builds
  too; it is not a debug-only build feature. Removing that definition compiles the module out. (It was originally gated
  on `NDEBUG` plus a `FLOWI_DEBUG_INSPECTOR` option, default OFF in release.)

## Considered alternatives

- **Promote `UiRenderCommand` to public `Fl*` ABI.** Rejected: it is the churniest possible type to
  freeze (mirrors Clay's command shape) and freezing it is precisely the ADR-0043 silent-break
  hazard, for an introspection-only feature no less.
- **Move the whole webserver (HTTP included) into flowi.** Rejected: flowi is a UI toolkit, not a
  network server; an HTTP/socket dependency violates the ladder (no new dependency for a transport
  concern) and the substrate-not-views principle. Only the IR-reading substrate belongs in flowi.
- **Leave it with the consumer and expose the IR behind a "we won't change it" gentleman's agreement.**
  Rejected: ADR-0043 exists because that agreement breaks silently with no compile error anywhere in
  the flowi tree.

## Consequences

- The inspector reads the IR on flowi's side of the boundary; the IR is free to churn with Clay
  without coordinated downstream rebuilds.
- A coordinated cross-repo change was required: flowi gained the inspector seam (done); the
  downstream webserver is to shrink from ~744 lines to an HTTP transport+routing shim over the seam,
  a migration that lives outside this repository.
- Dropping `FLOWI_UI_INSPECTOR` from the `flowi_objects` target compiles the module out.
