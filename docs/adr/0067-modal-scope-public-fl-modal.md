# Modal scope is public (`fl_modal_*`); the message box stays the canned widget over it

## Status

accepted — amends ADR-0017

## Context

ADR-0017 put a single-call `fl_popup_message_box` over the internal `ui_modal_*` scope and
deliberately kept the raw scope internal *for the message-box slice*. It also anticipated reuse —
it named the result type `FlDialogResult` generically "so later dialog widgets can reuse the same
one-shot result contract." Those later dialogs have arrived: downstream needs rich custom-body
dialogs (install / OSD / system) that render their own layout inside a modal scope, not the canned
title + message + button-row the message box draws.

The modal scope is **UI-core focus plumbing**, not app code. `ui_modal_begin_frame` / `begin` /
`end` / `close` touch `g_ui_ctx` focus internals (`saved_focused_id`, `popup_focus_scope_id`,
`current_popup_scope_id`, `modal_scope_id`, `popup_needs_initial_focus`) across ~20 sites to do
focus save/restore and frame-level input gating. An app cannot rebuild that without flowi exposing
those focus internals — which is exactly the `UiCtx` leak the toolkit avoids. So the *scope* is
toolkit; only the dialog *content* is app. Re-homing the scope to the app was therefore rejected.

## Decision

Promote the four modal-scope primitives to the public surface as `fl_modal_*` via a new `modal.def`
api block (`api Modal` → `fl_modal_begin_frame` / `fl_modal_begin` / `fl_modal_end` /
`fl_modal_close`). Rename-don't-forward: the bodies in `ui/ui.c` are renamed to `fl_modal_*` and
satisfy the generated `<flowi/ui/modal.h>` prototypes directly (ADR-0032 direct-call). No
`ui_modal_*` twins or forwarders remain for the promoted four.

`fl_popup_message_box` is unchanged — it stays the canned confirm/alert widget and now visibly sits
*over* the same public scope rather than over a hidden one. ADR-0017's reasoning for the single-call
shape (floating `attach_to = Root` decouples emit from draw; `popup_focus_scope_id` persists across
frames) is unaffected.

`ui_modal_is_active` is left internal — it is not part of the promoted four, has no callers, and
promoting or deleting it is out of this change's scope.

## Considered alternatives

- **Re-home the modal scope to the app.** Rejected: the scope mutates `g_ui_ctx` focus internals in
  ~20 places; rebuilding it app-side requires flowi to expose those internals — the precise leak the
  toolkit exists to prevent.
- **Keep the scope internal; widen the message box to take a custom body callback.** Rejected: it
  bends the canned single-call widget into a general container it was deliberately *not* (ADR-0017),
  and a body callback re-exposes the scope concept anyway, just less honestly than naming it.
- **Forwarders (`ui_modal_*` calling `fl_modal_*` or vice-versa).** Rejected: ADR-0032 is direct-call;
  a forwarder layer is the duplicate surface that ADR set out to remove. Rename the bodies instead.

## Consequences

- `fl_modal_begin_frame` / `begin` / `end` / `close` are public; downstream rich dialogs drive their
  own body through the toolkit-owned scope, and the ergonomic Rust façade exposes them as `Modal`.
- `fl_popup_message_box` and `FlDialogResult` are untouched; ADR-0017's single-call rationale stands.
- The modal scope is now a supported surface: its focus save/restore contract (saved on
  `begin_frame`, restored on `close`) is pinned by a `unit_tests` case so a regression in the focus
  plumbing trips a test rather than silently breaking every dialog built over it.
