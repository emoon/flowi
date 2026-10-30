# List widget: interaction mechanics, not model policy; selection crosses as requests

The List widget (CONTEXT: List, Item Id, Selection Request, Drop Relationship) owns the
*interaction mechanics* of a scrollable item column — (multi-)selection gestures, keyboard
nav, click-vs-drag threshold, and drag-reorder including the ghost and insertion-line
visuals — but deliberately owns **no model policy**. The app owns what an item is, the
selection set storage, drop validity, and how a reorder mutates its data. This follows Dear
ImGui's `BeginMultiSelect` split and keeps the List on the app side of the
mechanics/policy boundary: selection sets, drag-reorder policy, and DnD policy stay app-side — the List
is a thin reusable reorder-list widget, a *convenience*, not new substrate.

The load-bearing, hard-to-reverse choice is **how a selection change crosses back to the
app**: the List emits a per-frame array of [[Selection Request]]s (`ClearAll` / `SetAll` /
`SetOne` / `SetRange`) keyed on app-supplied opaque `u64` **stable ids**, with ranges
expressed by their two **endpoint ids** — and the app (owning the full ordered model) applies
them, expanding any range across rows that are virtualized or tree-collapsed away. The List
remembers only the gesture anchor across frames; it never stores the resolved set. By default
it auto-applies into a caller-owned `FlSelectionSet` helper so the common call site is one
line; the raw request array is opt-in for undo-coalescing.

## Considered Options

- **Widget returns the resolved `Set<u64>` each frame** (batteries-included): dead-simple call
  site, but it can only return what it can reconstruct, and it cannot reconstruct a set from
  only the visible items — so it must *store* the set (moving storage into the framework) and
  *still* needs an app-provided enumeration callback to shift-select across off-screen/
  collapsed rows. `SetAll` (Ctrl+A) becomes O(n) and un-virtualizable. Rejected: simplicity
  evaporates exactly where a sophisticated list needs it, and set ownership lands in the wrong
  place.
- **Range-by-span (enumerate every id in the range)**: breaks the moment the middle of a range
  is scrolled off-screen — the List doesn't have those ids. Endpoint-only is the only
  virtualization-safe formulation.
- **Keying selection on dense visible index instead of a stable id**: silently re-selects
  different rows after a sort/filter and forgets selection across tree collapse. Rejected; see
  Item Id.

## Consequences

- The app must be able to walk its model between its endpoint ids (`id` and `to_id`) to apply a range request —
  trivial for an owned array, and the same enumeration a tree already needs.
- Selection storage is always app-owned (`FlSelectionSet`); the List never interprets the
  `u64` beyond identity. Undo coalescing is a clean app-side seam over the request array.
- Tree-readiness is mostly *already bought* by these choices: a tree is a per-frame
  re-flattened list of visible nodes, stable ids survive collapse, and range requests
  delegate span-expansion to the app. The only tree-specific forward insurance is the
  [[Drop Relationship]] (`Before`/`After`/`Into`) on the reorder request, with `Into` dormant
  until the tree ships.
- v1 reorder is **intra-list only**; cross-list drag and OS-shell drag-out remain the separate,
  missing platform seam. The List draws default ghost/insertion-line visuals (overridable),
  so callers never hand-draw drag affordances.
- Variable-height rows and group headers are deferred: v1 reuses `list_uniform`'s
  uniform-height virtualization core rather than forking a third scroll engine.
