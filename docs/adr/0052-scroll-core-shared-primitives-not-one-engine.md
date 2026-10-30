# Scroll consolidation is a shared pure `scroll_core`, not one stateful engine — and `FlListClipper` stays a separate public shim

Status: accepted. Partly superseded by later code: `content_view` no longer exists, so `scroll_core` is used by
`ui_scroll`, `list_uniform` and `ui_menu` (`scroll_max`); and the Clay-leak follow-up below has since been done.


The scroll/virtualization consolidation (audit 2026-06-30, third candidate) is implemented as a
new `scroll_core.{c,h}` holding **pure primitives** — the `scroll_smooth_step` lerp,
`scroll_visible_range`, `scroll_max`, and one default smooth-scroll speed — that `ui_scroll`,
`content_view`, and `list_uniform` call. It is **not** a stateful "scroll engine" object that
the widgets become handles onto, and it does **not** introduce a shared `{offset, target}`
value-struct: each widget keeps its own id-keyed state hashmap with its own flat
`scroll_offset`/`scroll_target` fields and its own extras (`ui_scroll`: 2-axis + hover +
measured sizes; the two virtualizers: 1-axis + `selected_index`). Only the *functions* are
shared. Those three functions already existed and were already called by all three widgets —
but they lived in `ui_scroll.h`, the scroll-*area widget's* header, so `content_view` and
`list_uniform` `#include`d a sibling widget just to reach them. Extraction turns that layering
inversion into a real core with `ui_scroll` as a peer adapter. A shared value-struct was
considered and dropped: since all three widgets already carry flat offset/target fields and
already call the shared step, repackaging them into a struct would churn ~77 call sites (and
`ui_scroll`'s 2-axis `FlVec2` state does not fit a single-axis struct) to buy nothing the ADR
requires.

The load-bearing, hard-to-reverse choice is **what deliberately did *not* get merged**, because
the audit's headline ("consolidate the four … into one core") reads as a mandate to collapse
all four call sites, and a future reader will see four files still standing and assume the work
is unfinished.

- **`FlListClipper` is left standalone.** It is a *public, api-gen-generated, ABI-locked* range
  calculator (the ImGui `ImGuiListClipper` analog, app-facing, for app-owned Placed-child
  virtualization) with **zero internal callers**. Its visible-window math differs from the core:
  the clipper computes `last` from the *absolute* `(offset+viewport)/stride + 1`, while
  `scroll_visible_range` uses a *viewport-relative* count. They agree on stride-aligned offsets
  (and on the locked unit test) but **diverge during fractional scroll** — e.g. `offset=19,
  vp=2, stride=20` gives the clipper 2 rows (it catches the straddling row) and
  `scroll_visible_range` 1. Routing the clipper through the core (`buffer=0`) would drop that
  row → a visible gap at the viewport bottom, a behavioral regression on public surface, to save
  four trivial lines. So the clipper stays; its "consolidation" is a doc note only.

- **The smooth-scroll speed stays a per-caller parameter, not one value.** `smooth_scroll_step`
  already takes `speed` as an argument. `scroll_core` owns one default (`8.0`); `list_uniform`
  passes `12.0` (a faster snap so the selection highlight keeps up during keyboard nav). The
  win is one code path and one *default*, not literally one constant.

## Considered Options

- **Build a single stateful virtualization engine** the four widgets become adapters/handles
  over. Rejected: the pure math core was already ~80% shared; a new engine object invents a
  noun and a lifecycle to solve a bookkeeping-duplication problem that an embedded value-struct
  solves with no new type. The audit's own wins (one tuning default, locality, one Clay
  chokepoint) need no engine object.

- **Merge `FlListClipper` onto `scroll_visible_range` for true single-source.** Rejected: it is
  public + ABI-locked with subtly different (arguably more correct) window math; unifying
  regresses fractional-scroll row coverage. The fourth "entry point" is a public shim, not
  internal duplication.

- **Unify the three state hashmaps into one shared store, or embed a shared `{offset,target}`
  value-struct in each.** Rejected: the payloads differ (axis count, selection, hover, measured
  height) so a shared store forces a union-ish struct and merging three element-id namespaces
  into one table invites the duplicate-id fatal error the framework guards against; a mere
  value-struct, meanwhile, only repackages flat fields the widgets already have and already feed
  to the shared step. Share the logic, not the storage.

- **Collapse all speeds to one value and drop the `speed` parameter.** Rejected: `list_uniform`'s
  `12.0` is a plausibly-deliberate feel difference; the shared signature already carries `speed`
  for free, so a per-caller override costs nothing and preserves the feel.

## Consequences

- `scroll_core.{c,h}` is the single home for scroll math; `ui_scroll`, `content_view`, and
  `list_uniform` include it instead of `ui_scroll.h`. Scroll bugs land in one module.

- **Not in this slice — follow-up, since done:** the extraction itself left the Clay leak in the scroll
  widgets unsealed (`ui_scroll` and `list_uniform` called `Clay_GetElementData` /
  `Clay_GetScrollContainerData` directly). They now read element bounds through
  `ui_get_element_state_by_id` (its `FlElementState` carries `pos`/`size`/`found`), and the generic
  scroll-area content extent goes through the single wrapper `ui_get_scroll_container_extent` in `layout.c`,
  so neither widget file calls `Clay_` any more. This ADR records the scroll-math extraction.

- `list_uniform`'s `12.0` is marked as an unverified inherited feel value (history is
  inconclusive: both constants trace to the file-move commit with no dedicated tuning
  commit, and `list_uniform` comments its scroll policy as "like content_view" yet content_view
  runs `8.0`). If it is drift, it is a one-line flip to the `scroll_core` default with no
  re-plumbing.

- Latent, out of scope: `scroll_visible_range`'s viewport-relative count can miss a straddle row
  when the offset is unaligned — masked today only because the widgets pass `buffer=2`. The
  `FlListClipper` divergence above is the same math weakness seen without the buffer cushion.
