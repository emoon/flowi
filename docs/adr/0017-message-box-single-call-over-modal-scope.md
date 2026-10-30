# Message box is a single-call widget over the modal scope (no begin-frame split)

> **Amended by ADR-0067:** this ADR stands in full — `fl_popup_message_box` remains the canned
> single-call confirm/alert widget. ADR-0067 *adds* the sibling path this ADR anticipated: the raw
> modal scope is now promoted to the public `fl_modal_*` surface for rich custom-body dialogs, so
> "the raw `ui_modal_*` primitives stay internal" below is superseded — they are public `fl_modal_*`.

## Status

accepted — amended by ADR-0067

## Context

The toolkit needs a high-level confirm/alert dialog over the existing internal modal
scope (`ui_modal_*`, now `fl_modal_*`). An embedding host application already ships this shape twice,
each with a **two-call** structure: a
`*_begin_frame(state)` call at the very top of the frame (which arms `ui_modal_begin_frame`
so input-blocking covers widgets emitted before the dialog body) plus a `*_show(state, ...)`
call rendered inline, mid-layout, where the dialog visually sits. Both also thread a
caller-owned **state struct**.

A reasonable reader porting that pattern would assume `flowi`'s message box needs the same
`begin_frame` split, and would wonder why it doesn't. This ADR records why it doesn't.

## Decision

`fl_popup_message_box(title, message, bool* open, config)` is a **single call** returning
`FlDialogResult` (`None` while open; the one-shot decision `Confirmed`/`Cancelled` on the
dismissal frame, which also clears `*open`). It internally runs `ui_modal_begin_frame` →
floating backdrop/panel → `ui_modal_end`, and `ui_modal_close` on dismissal. The caller owns
only a `bool* open`; the function early-returns `None` when `!*open`, so it may be called
unconditionally. The raw modal primitives stayed internal when this was written (public as `fl_modal_*` since ADR-0067).

Two `flowi` properties make the `begin_frame` split unnecessary here, where it was necessary
for the host's dialogs:

- **The dialog renders into a floating area (`attach_to = Root`).** Emit position is
  decoupled from draw position, so the whole dialog can be emitted first in the frame and
  still draw on top — no need to split an early `begin_frame` away from a mid-layout body.
- **`popup_focus_scope_id` persists across frames** (cleared only by `ui_modal_close`; the
  frame cleanup at `ui.c` is skipped while `modal_scope_id != 0`). So background
  input-blocking is correct from the second frame onward regardless of call order. Only the
  single opening frame depends on the call sitting before the main content.

A message box also needs **no per-frame state beyond `open`**: focus save/restore lives in
the UI core (`s_ctx->saved_focused_id`, set by `ui_modal_begin_frame`), and button focus is
the existing focus system — so the host dialogs' state struct (which exists to persist
choice-list selection + a checkbox, plus a vestigial unused `saved_focus_id`) does not apply.

## Considered alternatives

- **Two-call `begin_frame` split (match the host dialogs exactly).** Rejected: it makes
  opening-frame input-blocking airtight with zero ordering rule, but costs a second public
  call and leaks the modal-scope concept back into the surface this slice deliberately hides.
  The residual exposure — opening-frame blocking depends on the single call sitting before
  the main content — is acceptable because the input that opened the dialog is on the
  foreground anyway, and steady state is airtight via scope persistence.
- **Caller-owned state struct (match the host dialogs' `*State`).** Rejected: a message
  box has no choice-list/checkbox state to persist, and focus save/restore already lives in
  the core. A bare `bool* open` is the whole contract.

## Consequences

- Soft ordering contract: callers should invoke `fl_popup_message_box` before their main content
  so the opening frame blocks background input. The widget gallery models this; the rule is
  documented at the call.
- `ESC` always returns `Cancelled` (no special-casing for single-button alerts); the dimmed
  backdrop captures the pointer but does not dismiss on outside-click. Initial focus is on
  the primary (OK/Confirm) button.
- `FlDialogResult` is named generically (not `FlMessageBoxResult`) so later dialog widgets
  can reuse the same one-shot result contract.
