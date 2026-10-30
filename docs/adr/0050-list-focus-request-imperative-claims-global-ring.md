# List focus-request is an imperative seam that claims the global focus ring

ADR-0049 forced "one additive List change": an opt-in way for a freshly-appeared popup (the
List-backed [[Dropdown]]) to start keyboard focus in the list, since today `has_focus` is set
only by a row click or a Tab-arrival and `fl_set_focus()` writes only the global `focused_id`
(never engaging the List's keyboard handling). That ADR left the *shape* of the seam an open
fork (`FlListOpts.focus_on_appear` *or* `ui_list_request_focus`). We resolve it as an
**imperative free function** — `void ui_list_request_focus(FlLayoutId id, u64 cursor_id)` — that
the caller invokes on the open edge, **not** a config field. It does two load-bearing things
together: it engages the List's own keyboard focus (`has_focus`) **and** claims the global
keyboard focus ring (`ui_ctx->focused_id = clay_id`). The [[List]]'s selection/activation
gesture contract (ADR-0028) is untouched — this grants focus, it does not change gestures.

`cursor_id` is the [[Item Id]] (stable id) to land the [[Focus Cursor]] on; `0` means "first
row". The List validates it against the ordered slice inside `begin` (the model is not known at
the call site, which runs before `begin`) and falls back to `ordered_ids[0]` when `cursor_id` is
`0` or absent. A non-searchable dropdown passes the current value's id (`*selected_index + 1`,
the 1-based id from ADR-0049) so the cursor opens on — and the focus ring agrees with — the
already-highlighted current option.

## Considered Options

- **`FlListOpts.focus_on_appear` config bool**: reads naturally on the declarative `ui_list`
  call, but the bool is passed *every* frame the popup re-emits, so it cannot literally mean
  "take focus now" without an internal edge-detector — and a naive read re-seeds the cursor each
  frame, stomping the user's arrow-key navigation. It also turns the pure layout/policy
  `FlListOpts`/`FlListConfig` structs into structs with a field whose meaning is "but only on
  some frames." Rejected: the trigger is an *event* (the popup opened), not a layout property.
- **Set `has_focus` without claiming the global ring** (the mirror of `fl_set_focus`'s gap):
  dead one frame later. `list_begin` blurs `has_focus` whenever the global `focused_id` belongs
  to another widget — and right after the trigger button is clicked, it does. Rejected: silently
  broken.
- **Add a `commit-on-click` / focus mode to `ui_list`** instead of a separate entry point:
  pushes dropdown policy into the List and muddies ADR-0028. Rejected for the same reason
  ADR-0049 rejected it for the click path — the caller is the right place.

## Consequences

- **The seam claims the global focus ring**, so opening the popup **blurs the trigger button**
  (it loses `focused_id`). This is a real, observable focus transfer, and it is the intended
  behavior — focus visibly moves into the popup and persists across frames (next frame's
  `list_begin` sees `global_focus == list_clay_id` and leaves `has_focus` engaged).
- **Re-arm guard**: the cursor is seeded only when the List did **not** already hold focus, so the
  genuine open-edge call seeds while a stray every-frame call is merely redundant instead of yanking
  the cursor back each frame. "Already holds focus" tests both `st->has_focus` **and** ownership of
  the global ring (`ui_ctx->focused_id == clay_id`), not `has_focus` alone: `has_focus` is never
  cleared while the popup is closed (the List is not emitted, so `begin` never blurs it), so a plain
  `!st->has_focus` guard would mistake a genuine reopen for a redundant call and leave a stale cursor
  instead of reseeding onto the current value. On a real open edge the just-clicked trigger owns the
  global ring (`focused_id != clay_id`), so the guard seeds; a stray call while the popup is open
  finds the List already owning the ring and is a no-op. The intended call site is the frame
  `is_open` flips false→true.
- **The cursor roves, the highlight stays.** In a single-select dropdown a plain arrow move
  moves the [[Focus Cursor]] but emits no [[Selection Request]] (it must not — ADR-0049 commits
  on any selection-change request, so an arrowing list would commit-and-close on every press).
  The selection highlight therefore stays on the current value while the focus ring roves;
  Enter/Space commit the cursor row via `activated_id → index = id - 1`. Making the *highlight*
  track the cursor (re-seeding the transient highlight set from `result.focused_id` each frame)
  is a deferred polish, not part of this seam.
- **api_gen**: the change is a new *function* in `api/ui_list.def` (exported as `fl_list_request_focus`, with regenerated C
  headers and Rust bindings); the `api/list.def` structs (`FlListConfig`/`FlListOpts`) are untouched — there is no new field.
- **Scope**: only the non-searchable dropdown requests focus on open. A searchable dropdown must
  start focus in its search input (ADR-0049), so the input↔list focus hand-off is a separate,
  later slice.
