# Dock OS-window promotion via an injected host seam; flowi owns the decision, the host owns the mechanics

## Status

accepted

## Context

ADR-0040 settled that a Detachable Panel may be **promoted** to its own compositor-placed OS
window, deferred behind the internal-floating interaction. ADR-0039 gave the dock tree its data
model (topology + ratios, mutated only by `apply(tree, request)` at one safe point) and ADR-0038
split state into a per-window context over a shared tier. The headless half of promotion already
landed: `dock_workspace_promote`/`demote` move a panel's subtree between two in-process
workspaces, no OS seam, fully unit-tested.

What remained is wiring that to **real OS windows**. Two forces constrain how:

- **flowi must not open windows or name global coordinates.** ADR-0040's whole point is that the
  compositor places promoted windows (Wayland denies a client position/global-cursor). ADR-0038's
  framing is "the runner brings up the shared tier and attaches windows." So the core cannot call
  `fl_app_window_open` — that lives in the `fl_app` runner, above the core.
- **A single workspace per app is the structural blocker.** `UiShared` held one `dock_workspace`
  and `dock_workspace_current()` returned it for every window. A promoted window needs to render a
  *different* tree than the main window, so a single shared workspace cannot express promotion.

The existing backend seams (`FlRenderBackend`, `FlWindowBackend`) already show the shape for "the
core needs a capability the host owns": an injected vtable the host constructs and the core calls.

## Decision

Implement promotion as **flowi-owns-the-decision / host-owns-the-mechanics**, over a workspace
registry.

- **Workspace registry in the shared tier.** Replace the single `UiShared.dock_workspace` with a
  registry of workspaces (entry 0 is the former single workspace). The *set* of trees is
  app-global and lives in the shared tier (so it survives a promote/demote and is not held in a
  per-window context); each tree belongs to one workspace hosted by one window.
- **Per-window binding = a non-owning `FlDockWorkspace*` on `FlUiCtx`** (`ui_ctx_set_dock_workspace`;
  ADR-0038 amendment). `dock_workspace_current()` resolves `ctx->dock_workspace`. A plain window
  defaults to registry entry 0, so single-window behaviour is unchanged.
- **Injected `FlDockHost` vtable** for the OS seam:
  `open_promoted_window(ud, ws, w, h) -> opaque handle` and `close_promoted_window(ud, handle)`.
  The host constructs it; flowi calls it. flowi never opens a window or sees a global coordinate.
- **Promote is a deferred request.** `dock_promote_panel(id)` enqueues; the request is applied at
  the docking layer's single safe point (`dock_workspace_begin`, before the emit walk — the same
  never-mid-walk discipline as every other structural change). At that point: create the second
  workspace, `dock_workspace_promote(src, dst, id)`, `host->open_promoted_window(dst, …)`, record
  `{handle, dst, src, id}`.
- **Transparent render.** A promoted window reuses the app's **same** dock frame fn; the runner
  feeds it the main window's callback. `dock_workspace_current()` (resolving via the window's
  binding) gates which panels emit, so `dock_panel(id){…}` runs a body only for the panels in
  *that* window's tree. The app writes **zero** new emit code for promotion.
- **Close = demote back.** The host wires the promoted window's `on_close` to
  `fl_dock_notify_window_closed(handle)`; flowi then runs `dock_workspace_demote` (rebinding into
  the source by stable id via the dock rebind rules; degrading to the floor/root rule if the
  old leaf is gone), destroys the second workspace, and drops the registry entry.
- **Default host in `fl_app`.** `fl_app_create` registers a default `FlDockHost`
  (`open_promoted_window` → a queued `fl_app_window_open` with the dst workspace stashed on the
  pending-open record, bound right after `ui_create` at materialization), so any `fl_app` app gets
  promotion for free; an app adds only a trigger.

## Considered alternatives

- **Core calls `fl_app_window_open` directly.** Rejected: it inverts the ADR-0038 dependency (core
  would depend on the runner) and bakes window mechanics into a layer that must stay
  backing-agnostic and headless-testable.
- **Promoted window state stored in its per-window context.** Rejected by ADR-0038: it would reset
  the panel's scroll/text/expansion on promote. The registry keeps the tree app-global and the
  id-keyed state in the shared store.
- **Apply the promote immediately when `dock_promote_panel` is called.** Rejected: that call lands
  mid-frame (inside the app's emit), and mutating a source tree mid-walk is the exact hazard the
  deferred-request discipline exists to prevent. Enqueue + drain at `dock_workspace_begin`.
- **A bespoke per-promotion callback instead of a vtable.** Rejected: the `FlRenderBackend` /
  `FlWindowBackend` vtable shape is the established pattern, and a registered host also serves the
  symmetric `close_promoted_window` (the flowi-initiated teardown path).

## Consequences

- The promote *decision* and the demote rebind stay pure, in-process, and headless-testable; only
  the window open/close crosses the OS seam, which a stub host stands in for in `unit_tests`.
- Compositor placement itself (Hyprland tiling the new window) cannot be asserted headlessly, so
  it is a manual Wayland verification step (the checklist is in the `dock_demo` example README).
- The host seam carries `close_promoted_window` for a flowi-initiated teardown (e.g. a future
  programmatic demote); the user/WM-initiated path runs through `fl_dock_notify_window_closed`.
- **Cross-window tab re-homing is out of scope** (ADR-0040 amendment): dragging a tab from window
  A into B's tree needs the carried-panel model, a separate follow-up.
