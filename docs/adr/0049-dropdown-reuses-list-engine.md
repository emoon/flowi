# Dropdown is implemented on the List engine; it reinterprets selection as a single commit

The [[Dropdown]] (`fl_dropdown`) keeps its identity as a *separate, simpler* widget at the API
and concept level — a homogeneous string picker over `FlString[]` returning an
`int* selected_index`, with caller-owned `is_open` — but its **open popup is now built from the
[[List]]** (`ui_list`, declarative face, `multi_select=false`, `reorderable=false`) instead of a
hand-rolled loop of Ghost-variant buttons capped at 50 items. This buys the dropdown the three
things its old composite had no path to: **virtualization** (no item cap), **real keyboard
navigation** (arrow keys, Home/End), and **type-ahead** — all already living in the List per
ADR-0028, none of which the button loop could grow without reimplementing the List.

The load-bearing, hard-to-reverse choice is **how the List's two-gesture model collapses to the
dropdown's one**. The List deliberately splits *click = select* from *double-click/Enter =
activate* (ADR-0028); a dropdown commits on a single click. Rather than add a "commit-on-click"
mode to the List (which would erode its gesture contract), the **dropdown layer reinterprets the
List's output**: it treats any per-frame selection-change request (a mouse click) *or*
`activated_id` (Enter) as "the user picked" → writes `*selected_index` and sets `*is_open=false`.
The List's *gesture model* is unmodified; the dropdown is a thin interpreter over it. The reuse
does force **one** additive, unrelated List change — an opt-in focus-request so a freshly-opened
popup can start keyboard focus in the list (see Consequences); without it arrow-nav and
Enter-to-commit are dead until the user clicks a row.

Keyboard ownership between filtering and type-ahead is **routed by focus, not contested**: a
searchable dropdown renders a real visible `ui_text_input` on top of the list (the
earlier `searchable` path read keystrokes into an *invisible* buffer — a latent gap this closed). While
focus is in the input, typing filters the option set (the dropdown computes the matching subset
and passes only that to the List); pressing Down hands focus into the List, where typing becomes
type-ahead and Up at the first row (`nav_up_edge`) hands focus back to the input. A non-searchable
dropdown has no input and focus starts in the List.

## Considered Options

- **Keep the two widgets separate (status quo)**: no shared engine, but the dropdown is then
  permanently stuck with the 50-item cap, no virtualization, and no arrow-key nav — exactly the
  capabilities the List exists to provide. Rejected: the duplication is the *valuable* code, not
  incidental boilerplate.
- **Add a `commit-on-click` mode to `ui_list`**: cleaner dropdown call site, but it pushes
  dropdown policy into the List and muddies the ADR-0028 select-vs-activate split that the List's
  multi-select gestures depend on. Rejected: the reinterpretation lives better in the caller.
- **Expose a new list-backed public combo API (stable ids + selection set)**: maximal power, but
  breaks every `fl_dropdown` call site and re-exposes the very model policy the dropdown exists to
  hide. Rejected: the reuse is an implementation concern, not an API change.
- **Fold the Menu onto the List too**: rejected — the Menu is heterogeneous, nested, and
  built-not-virtualized (ADR-0019); the List is the wrong substrate. The only shared seam is the
  type-ahead matcher.

## Consequences

- Option **stable ids are 1-based** (`id = index + 1`): the List reserves `0` as the "none"
  sentinel for `activated_id`/`focused_id`/anchor, so a 0-based index would make the first option
  indistinguishable from "nothing selected".
- The dropdown stays **stateless beyond `is_open` + `selected_index`**: the List needs a
  `FlSelectionSet` only to highlight the current row, so the dropdown builds a **transient
  one-element set** seeded from `*selected_index` each open frame, feeds it for highlight, and
  never reads it back (the pick comes from the List result) or persists it. `selection_set_create`
  allocates a heap arena, so the create/destroy must be **paired every open frame** or it leaks an
  arena per frame.
- The reuse forces **one additive List change**: an opt-in focus-request (landed as
  `ui_list_request_focus`, ADR-0050) that sets the List's `has_focus` and
  seeds the focus cursor. Today `has_focus` is set only by a row click or Tab+nav-key, so a
  freshly-opened popup otherwise has dead arrow-keys and no Enter-to-commit. It is **opt-in**
  because a *searchable* dropdown must start focus in the search input, not the list. The List's
  selection/activation gesture contract (ADR-0028) is untouched.
- The popup commits to a **bounded viewport** — `min(count, max_visible_rows) * row_stride`
  (new additive `FlDropdownConfig.max_visible_rows`, default 10) — and **the 50-item cap is
  deleted**. The trigger button, popup anchoring (`ui_popup`), and optional cancel row (a plain
  button below the list) are unchanged dropdown-layer concerns.
- Dropdown option rows now render as List rows (label only, no icon), which is the cross-widget
  look/behavior consistency the change set out to get.
- The **type-ahead matcher is extracted** from `ui_list.c` into a standalone helper so the List
  owns one implementation that any later consumer can share (the Menu does not use it today); the dropdown
  consumes it transitively through the List. This is the one piece of genuinely new shared substrate.
