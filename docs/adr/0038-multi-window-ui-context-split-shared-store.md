# Multi-window UI context: per-window context over a shared resource + persistent-state tier

## Status

accepted

## Context

Detachable windows (a synth editor alongside a main window) are simultaneously interactive, each
with its own hover/active/keyboard-focus state, its own layout tree, and possibly a different
monitor/DPI. The app author must program to a backing-agnostic "Detachable Panel" and never
branch on whether a panel is an internal floating layer or a promoted OS window — so a panel's
state (scroll offset, half-typed text, tree expansion) must survive promotion unchanged.

Two facts in the code shape the decision:

- A **Clay context holds exactly one layout tree**. The main-window tree and the synth tree are
  different trees that must both be laid out in the same frame, so they cannot share one Clay
  context. (Each `FlUiCtx` now owns its own Clay context from `Clay_Initialize` in `ui_create`, and
  `Clay_SetCurrentContext` selects it; the earlier process-global singleton is gone.)
- Widget ids are already **location-independent**: `ui_layout_id_to_clay_id` is
  `Clay__HashString(name, author_seed)` with no parent/position component, and persistent
  widget state is keyed on that hash.

## Decision

Split `FlUiCtx` across two lifetimes.

- **Per-window context** — one per `Window`: its own Clay context, input snapshot,
  hot/active, keyboard focus, `ui_scale`/`text_scale` (DPI), popup/menu/nav stacks, and the
  frame arena. Forced by the one-tree-per-Clay-context constraint; also correct (each window
  has its own cursor-relative coords and DPI). A `ui_make_current(ctx)` swaps `g_ui_ctx` and
  `Clay_SetCurrentContext` before a window emits its frame.
- **Shared tier** — one per app: the `FlRenderBackend`, font cache, `Image Registry`, base
  `Theme`, icons, scaled fonts, **and the persistent id-keyed widget-state store**
  (`item_state`, scroll offsets, text-input, tree expansion) keyed by the hashed id. Because
  this state never moves between contexts, a Detachable Panel keeps it across a promote/demote
  for free.

Keyboard **focus is per-window** (each window restores its own focused field); a hidden-drag
holds `active_id` in the window that began it, enforced by an OS mouse-grab.

## Considered alternatives

- **One shared `FlUiCtx`, N surfaces.** Rejected: a single Clay context cannot hold two
  different layout trees, and one set of hot/active/focus fields cannot represent two live
  windows.
- **One `FlUiCtx` per window with state living *in* each context.** Rejected: promoting a panel
  to its own window would reset its scroll/text/expansion (the state would sit in the wrong
  context) — exactly the transparency the Detachable Panel model forbids.
- **Migrate id-keyed state between contexts on promote/demote.** Rejected: location-independent
  ids make a shared store strictly simpler, with no copy step and no id-stability dance.

## Consequences

- The within-window id-uniqueness assumption (and the debug duplicate-id tracker) becomes
  **app-global**; detachable panels should seed their subtree under a per-panel namespace to
  avoid cross-window id collisions.
- The per-window `theme_override` stack and other per-frame subtree state stay in the
  per-window context, not the shared store.
- `ui_create` splits into a shared constructor (`ui_shared_create`) + a per-window constructor; the runner brings
  up the shared tier once and attaches each window to it.

## Amendment (2026-06-22): a non-owning `dock_workspace*` on the per-window context is permitted

OS-window promotion (ADR-0041) needs each window to render a *different* dock tree: the main
window its workspace, a promoted window its own. The dock *trees* (the topology + ratios) still
live entirely in the shared tier — the set of workspaces is an app-global registry there, and a
panel's id-keyed state still survives a promote/demote for free, exactly as this ADR requires.
What is added is a **non-owning** `FlDockWorkspace*` on the per-window `FlUiCtx` (set via
`ui_ctx_set_dock_workspace`) naming *which* shared workspace this window hosts;
`dock_workspace_current()` resolves through it.

This does **not** reopen "state living in the context" (the rejected alternative above): the
pointer is a reference into the shared registry, not owned state, and it carries no
scroll/text/expansion — those remain in the shared id-keyed store. It is the per-window *view
selector* over app-global state, the same shape as `surface` or `clay_ctx` already on `FlUiCtx`. A
plain window defaults to registry entry 0, so single-window behaviour is unchanged.
