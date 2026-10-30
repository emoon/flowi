# Pointer/cursor platform seam: hidden relative drag + per-frame cursor request

## Status

accepted

## Context

Audio-style UIs need **knobs and faders** — and the synth use case behind the optional-tier
painter IR makes the knob the canonical control. A knob's *visual* (arc indicator) and its
*interaction* (drag-to-value, double-click reset, modifier fine-drag, wheel adjust) are already
bound: `Painter::arc` carries the ADR-0008 angle convention, and `fl_item_behavior` →
`FlSignal{ dragging, drag_delta, double_clicked }` is the bound generic behaviour. So the knob is a
**custom composite at the widget layer**, not a base C control — consistent with CONTEXT.md ("every
knob is its own control… built from these primitives").

But that composite has a floor that does not exist. A professional knob/fader/numeric-drag-field
scrubs an **unbounded** value: on press the OS cursor is hidden, the mouse enters relative motion,
the control reads raw deltas indefinitely, and on release the cursor is restored to where the drag
began (Vital does exactly this via JUCE — `source.hideCursor()` +
`enableUnboundedMouseMovement(true)`, then `false` on release). flowi has **none** of this:

- The core tracks only **absolute** mouse position (`input_state.mouse.{x,y,buttons,wheel}`); there
  is no delta. A position-derived `drag_delta` **stalls at the screen edge** and the visible cursor
  drifts off the knob — unusable for an infinite scrub.
- There is **no cursor control at all**: no shape, no hide/show, no relative mode, no warp. A
  tree-wide search found only absolute position and headless `inject_mouse_move`.

Such UIs also need **cursor shapes** beyond the knob — resize-H on a clip trim edge, hand for pan,
text for rename — and, later, **custom image cursors** for tool modes (razor, pencil, pan-grab).

Two facts about the platform layer decided the shape:

- **The `InputBackend` vtable already carries imperative platform-mode toggles** —
  `start_text_input` / `stop_text_input`. Cursor and relative-mode calls are the same shape, so they
  belong on the same seam (SDL implements them; headless/DRM stub). This is a capability gap — new C
  surface — not a binding of something that exists.
- **The app already owns the drag latch** (ADR-0022): it decides press→begin, held→read,
  release→end. The hidden drag is the *unbounded* sibling of that bounded latch; the app keeps the
  latch, the platform owns only the hide / relative-motion / warp-back mechanics.

## Decision

Add one **pointer/cursor platform seam** on `InputBackend`, surfaced through the public `fl_*` calls and
the Rust binding, with three facets:

1. **Hidden relative drag** — `mouse_hidden_drag_begin(scale)` / `mouse_hidden_drag_end()` as a
   bundled, app-owned pair. `begin` hides the cursor, enters relative/unbounded mode, and remembers
   the press point; `end` exits relative mode, **warps the cursor back to the press point**, and
   shows it. While active, `mouse_drag_delta()` returns the per-frame raw delta **already multiplied
   by the caller-supplied `scale`** (Vital's `setMouseDragSensitivity`, folded into the API). The
   app drives begin/held-read/end from its own latch.

2. **Cursor shape** — `fl_cursor_set(cursor)` taking a **uniform opaque `Cursor` handle**, requested
   **per frame, last-writer-wins, auto-reset to the arrow** when nobody requests one. Built-in
   **system shapes** (arrow, hand, resize-H/V/diagonal, text, crosshair, not-allowed) ship first via
   `Cursor::system(shape)`; **custom image cursors** (`Cursor::from_image(image, hotspot)`) are
   deferred but the handle type already admits them, so the set call never changes when they arrive.

3. **Cursor visibility** — a standalone `fl_cursor_set_visible(bool)` toggle (a hidden drag hides the
   cursor for its whole duration regardless).

The knob, fader-as-vertical-slider, and numeric drag-field are then composites/bindings over this
floor; a reference `examples/knob.rs` proves the composite. See CONTEXT.md **Hidden Drag** and
**Cursor**.

## Considered alternatives

- **Low-level cursor/relative primitives only** (`set_cursor_visible`, `set_relative_mouse`,
  `warp_mouse`, raw `mouse_delta`), letting each control compose the hide→relative→read→restore
  dance. **Rejected:** the three steps must always fire together, and the correctness-critical bit —
  warping the cursor back to *where it was grabbed* on release — is exactly what every knob and
  drag-field would otherwise reimplement and get subtly wrong. The bundle matches JUCE's single
  `enableUnboundedMouseMovement` granularity. Visibility and shape *are* exposed as standalone
  primitives, because they have non-drag uses.

- **Reading `Signal.drag_delta` for the scrub** (no relative mode). **Rejected:** it is
  position-derived, so it clamps at the screen edge and the visible cursor drifts off the control —
  the precise failure a hidden drag exists to escape.

- **A base C `ui_knob` widget** (like the slider). **Rejected:** knob visuals are the most
  skin-specific control in an audio UI (the optional painter tier exists *for* this); a base widget
  would be net-new C, over-fit one look, and contradict the "composite at the widget layer" line.
  The reusable part (drag→value math) is a few lines.

- **Sticky imperative cursor** (`set_cursor` persists until changed). **Rejected:** it forces every
  widget to reset the cursor on un-hover — bug-prone bookkeeping the per-frame request removes; a
  persistent tool cursor is trivially re-requested each frame.

- **A system-shape enum at the set call site.** **Rejected:** custom image cursors (a stated
  requirement) would then force an API change; a uniform handle absorbs both.

## Consequences

- New C platform surface on the `InputBackend` vtable (cursor shape/visibility, relative-drag
  begin/end, raw delta), implemented by the SDL backend and stubbed by headless/DRM. The core gains
  a raw relative-delta accumulator fed by relative-motion events, separate from absolute position.
- The headless harness needs a relative-delta inject path so the knob/drag scrub is testable end to
  end (inject delta → tick → assert value change), alongside the existing absolute `inject_mouse_move`.
- The knob, numeric drag-field, and timeline pan all build on `mouse_hidden_drag_*`; the fader is a
  vertical/logarithmic slider. None are base C widgets.
- Custom image cursors are deferred work, but no API breaks when they land — `Cursor` is already a
  handle and `fl_cursor_set` already takes it.
- This is the largest net-new piece of the binding work; the menu and slider gaps are
  pure bindings, and text-input/keys/double-click were already covered.
</content>
</invoke>
