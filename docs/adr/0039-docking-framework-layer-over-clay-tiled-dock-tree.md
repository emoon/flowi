# Docking: a framework layer over a Clay-tiled retained logical dock tree

## Status

accepted

## Context

A window docking system is the next major piece, driven by three apps: a debugger frontend
(**required**), a module rack, and a music player (metadata/visualizer panels).
Multi-window (ADR-0037/0038) was built as its prerequisite. The dominant failure mode of a
hand-rolled docker is "holes" — gaps, orphaned regions, panels lost on close — so the model
must make those *structurally* hard and exhaustively testable.

Two earlier framings need correcting. The CONTEXT.md "Detachable Panel" note ("the policy layer
— a docking system — is future") meant *not-yet-built*, **not** *belongs-to-the-app*; and the
earlier gap analyses that called docking "app-side" were inferences, not intent. The
user-transparency rule — *app code never branches on whether a panel is an OS window* — is the **reason** the Window Backend
abstraction exists, so docking can promote/demote a panel underneath byte-identical emit code.
Docking is therefore a **framework** concern.

Two facts in the codebase shape the model:

- **Clay computes layout from the declared tree each frame** (`sizing_percent` exists in
  `layout.h`; computed rects are readable by id via `ui_get_element_state` /
  `Clay_GetElementData().boundingBox`, already used in `ui_scroll.c` / `list_uniform.c`). So
  the framework need not store or compute any pixel geometry.
- **Widget ids are location-independent** and the **shared tier** (ADR-0038) already holds
  app-global, id-keyed persistent state that survives promote/demote.

Dear ImGui's docking branch was studied as prior art (`ImGuiDockNode`, `BeginDocked`,
`ImGuiDockRequest`, `ImGuiDockNodeSettings`, central node, `ImGuiWindowClass`). We borrow its
proven shapes and skip what Clay or our constraints make unnecessary.

## Decision

Build docking as a **framework layer above `FlUiCtx`** (never inside the widget core), structured
around a **retained logical dock tree** that Clay tiles.

- **Dock Node** is *exactly one of*: a **split** (two children + a split axis + one `ratio`
  scalar in (0,1)) or a **leaf** (a tab group of one-or-more Detachable Panel ids + the selected
  tab). The tree stores **topology + ratios only — never `Pos`/`Size`**. Each frame the layer
  walks the tree and emits nested Clay containers (split → a directional container with
  `sizing_percent(ratio)` children; leaf → a tab bar + the selected panel's emit scope) and
  **Clay computes every rect**. Node rects are read back by id for drag-dock hit-testing.
- **Immediate panels, retained tree.** A Detachable Panel is declared each frame as an inline
  `dock_panel(id){…}` scope (the existing house style) and bound to its node **by stable id**,
  mirroring imgui's `window->DockId` + `BeginDocked`. The tree lives in the **shared tier**
  (app-global, spanning all windows), so it survives promote/demote with no copy step.
- **No central node.** Clay `sizing_grow()` provides "absorbs remaining space"; the **root is
  the floor** (the only node exempt from empty-leaf collapse — it degrades to an empty leaf); a
  stable main area is an app convention (a never-closing panel), not an engine-distinguished
  node with propagated exemptions.
- **Workspace** = a dock tree hosted by an OS window *or* embedded as a Clay region, with an
  optional **dock-family filter**. A panel carries one family tag (default *general*); the engine
  enforces the filter at **drop-resolution** so an incompatible drop never highlights — it is
  impossible, not rejected after the fact.
- **Pure operations + deferred requests.** Structural change is a value
  `apply(tree, request) → tree'` (dock-as-tab, dock-split ×4, dock-outer, undock, reorder,
  close, splitter-resize), applied at one safe point (imgui's `ImGuiDockRequest` pattern), each
  ending invariant-clean. The drag-dock interaction only *builds* requests; the drop **preview**
  is `apply(committed, hovered_request)` rendered as a **reserved empty slot** — the **gap**.
- **The preview feeds back, deliberately** (amendment). Dragging is direct manipulation of the
  layout: the hit-test resolves against the **live provisional layout**, gap included, so there
  is no targeting UI behind the tiles and no way to miss. That inverts the obvious
  "preview must not feed back into the decision" property, so the loop is stabilised by two
  independent rules, both required — the gap oscillates in two unrelated ways:
  - **sticky gap** — from inside the gap the cursor may push it around the node it opened in,
    but may not hand it to a neighbour the gap itself displaced. The gap is never a hit-test
    candidate.
  - **pre-gap footprint** — the node the gap took its space from is hit-tested at the rect it
    held *before* the gap opened, so opening the gap cannot move the boundary that resolved it.

  The preview **tree is retained** across the whole drag rather than rebuilt per frame: cloned
  once at the first resolved zone, then mutated in place. A re-target is undock-then-request on
  the same tree. The gap's share rides the request's `ratio`, so the split the release commits is
  exactly the proportion the gap showed.
- **One tier, no tweening.** The tree is what Clay is fed: every op — a split insertion, a
  collapse, a hidden sibling dropping out — lands in full on the frame it is applied. A separate
  eased "display" tier was built and then removed: the gap is inside the geometry the next
  frame's hit-test resolves against, so a gap that is still moving is a boundary that is still
  moving, and the two stabilisation rules above have to hold against a target that never settles.
  Making the geometry land at once removes that whole class of feedback.
- **First-run layout** is a declarative literal in the `ui_layout_area` style, lowered onto the
  imperative builder and validated by the invariant checker at startup; authored in app code.
- **Persistence** (a layout blob store): the logical tree (topology + axes + target
  ratios + leaf panel-id lists + selected tab) + per-workspace family filter + per-workspace host
  *size* (not position, per ADR-0040) + a version field. Settings **outlive instantiation**.
  Two load-side rules keep a restored layout emittable, because floating is transient drag state
  and a panel has no persisted title or close affordance of its own:
  - **A load never parks a panel.** Reconcile ends with every floating set empty — each parked
    id is docked into its workspace's root region. The `floating` key stays in the blob (empty in
    practice, since a panel is only parked mid-drag) and is still parsed, so an older blob's
    parked panels re-dock rather than vanish.
  - **The caller's roster prunes.** A saved id the caller does not list is **closed**, not
    reserved, so it cannot become a permanent blank unkillable tab. A caller supplying no roster
    prunes nothing and keeps every saved panel reserved. Reservation itself stays frame-scoped:
    a panel the app declared before but not this frame keeps its slot.

## Considered alternatives

- **Docking as app-side policy** (each app rolls its own). Rejected: three apps would each
  reimplement a hole-free dock tree — exactly the duplication that breeds holes — and the
  transparency rule (no branching on OS-window-ness) needs engine enforcement at drop-resolution
  anyway.
- **imgui's per-node geometry** (`Pos`/`Size`/`SizeRef` + manual rect subdivision +
  `DockNodeTreeUpdatePosSize`). Rejected: Clay already tiles from `sizing_percent`, so storing
  geometry would duplicate the layout engine and reintroduce the gaps Clay makes impossible.
- **Central node** (imgui). Rejected: its two real benefits are free here — "remaining space"
  via `sizing_grow()`, "never vanishes" via the root-is-floor rule — and dropping it deletes an
  invariant (one central node, root-reachable, collapse-exempt) and its flag-inheritance.
- **imgui `ImGuiWindowClass` flag-override/inheritance.** Rejected the machinery, kept the idea:
  a per-workspace family filter + a per-panel family tag, no `DockNodeFlagsOverrideSet`.
- **Animating the layout at all**, either as a separate visual layer (FLIP/rect interpolation) or
  as an eased second copy of the ratios Clay consumes. The second form was built and shipped
  before being removed; see "One tier, no tweening" above.

## Consequences

- The within-window id-uniqueness assumption is already app-global (ADR-0038); panels should
  namespace their subtree ids to avoid cross-window collisions.
- Docking consumes substrate that was partial or missing when this was decided: splitter panes
  fall out of the split-divider drag (a ratio mutation, `dock_splitter.c`), tab bars gain dock
  affordances, and layout-blob persistence became real (`dock_persist.c`).
- The "no holes" guarantee is enforced by one chokepoint, `dock_tree_check_invariants(tree)`
  (split-xor-leaf; no single-child splits; ratios clamped; each panel-id in ≤1 leaf; root the
  only collapse-exempt node; selected tab is a leaf member), asserted after every operation. The
  centerpiece test is randomized op sequences with a **panel-id conservation law** (only `close`
  may remove a panel), all headless in `unit_tests` (the two-executable rule).
- Cross-window and detach behavior, and why OS-window *position* is not persisted, are governed
  by ADR-0040.
