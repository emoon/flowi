# Mouse buttons are global, unrouted free queries; the drag latch is owned by the app

## Status

accepted

## Context

An application needs mouse-button input to drive **drag-to-pan** over its timeline: press
over the arrangement, move, the view pans, release ends it. The wheel-input slice (ADR-0021)
deliberately deferred drag — "the press-capture state machine earns its place in the clip-editing
slice" — and the navigation gesture map (wheel / Shift+wheel / Ctrl+wheel) needed no buttons at
all. Pan is the first gesture that does.

This also forced a wider look at the input-query surface, which had grown one commit at a time and
was inconsistent: a modifier query with no `keyboard_` prefix, and a pointer-position query that
said "pointer" where everything else says "mouse" with the coordinate space baked into the name. So
the slice is a rename pass plus the new primitive, not just an addition.

Two facts about the C core decided the shape:

- **There is no public global mouse-button query.** `ui/ui.h` exposes `ui_key_pressed/down/released`
  for the keyboard and `ui_keyboard_modifiers()` / `ui_get_mouse_wheel()`, but nothing for mouse
  buttons. The state lives in private `ui_private.h` (`MouseState.buttons[]`,
  `buttons_pressed[]`, `buttons_released[]`). So unlike the trivial `keyboard_modifiers()` bind, this is a
  **capability gap** — new C surface, not just a binding.
- **Button state is global and needs no routing.** `platform/input.c` populates the
  per-button arrays from events; every reader sees the same state regardless of what is under the
  cursor. This is the **opposite** of the wheel, which ADR-0021 found *had* to be scoped/arbitrated
  because `ui_update_begin` routed the cached wheel to scroll containers — no such arbitration is
  needed for buttons.
- **But the edges are cleared before frame code runs, so they must be snapshotted.** The tick order
  is `pump -> fl_platform_input_update -> begin -> frame`, and `fl_platform_input_update` clears the per-frame edge
  arrays (`buttons_pressed` / `buttons_released`) *before* any frame code runs — so a raw read of
  those arrays from a frame query always sees `false`. The core already solves this for the
  Left-only `mouse_pressed_this_frame`: it snapshots the edge into the context before the clear.
  The held *level* (`buttons[]`) is not cleared and survives. So the binding generalizes that
  existing snapshot per-button rather than reading the raw arrays.

The forking question was therefore: model buttons like the wheel (a cooked, element-scoped,
consume-takes-all **drag** read reusing the core's `active_id`/`is_dragging` latch), or expose a
**raw global button primitive** and let the app build the drag itself.

## Decision

Mouse buttons are a **raw, global, unrouted free query**, and the drag latch lives in the app.

- **Three C free queries**, mirroring the keyboard idiom exactly: `ui_mouse_pressed(button)` (down
  edge), `ui_mouse_down(button)` (held level), `ui_mouse_released(button)` (up edge). They are
  **not** scoped to an element and need no arbitration. The *level* reads the surviving
  `MouseState.buttons[]`; the *edges* read a per-button snapshot the context captures before
  `fl_platform_input_update` clears the raw edge arrays — generalizing the core's existing Left-only
  `mouse_pressed_this_frame` cache so a frame-time read is reliable.
- **Global, not scoped.** Every reader sees the same button state regardless of what is under the
  cursor. There is no Scroll-Target-style routing — it would be ceremony with no payoff, since the
  arbitration that forced wheel routing does not apply to a global button read.
- **The app owns the drag latch.** A pan/drag is *not* a framework-cooked read. The app notes the
  *press* edge while the cursor is over its region, then follows the *held* level and the (logical,
  scale-correct) **mouse position** until *release* — even as the cursor leaves the region. This
  matches the "front-end owns all view state" invariant (the same reason ADR-0021 rejected
  the engine-managed scroll area) and sidesteps the core's `drag_delta`, which is physical-pixel
  and cumulative-from-start.
- **One ergonomic call, lossless.** The Rust binding composes the three queries into
  `mouse_button(button) -> { pressed, held, released }`, matching the core's existing
  `ActionState { pressed, held, released }` (`input_actions.h`). Three independent channels, not a
  single up/down value: a fast tap is `pressed && released` in one frame, and a just-pressed
  button is both `pressed` and `held` — a one-value enum cannot represent either.
- **Mirror-free seam.** The C queries return `bool` over the existing api_gen `Ui` seam; the
  composition into the state struct is a Rust-layer concern, so no new mirror / drift guard is
  added (the virtue both prior input slices preserved).
- **Naming cleanup, shipped together.** The modifier query gains a `keyboard_` prefix
  (`keyboard_modifiers()`) and the pointer-position query becomes `mouse_position(id)` (element-local
  **logical** space, semantics unchanged). _When this ADR was written the rename lived only in the
  ergonomic `flowi` crate, with the C and api_gen seam names left on the old spelling to avoid
  desyncing them from the then-current vendored input vtable. That split was later removed: after the
  internal vtable was dropped (ADR-0032) the `.def` became the single source of truth, so the
  ergonomic names were pushed down through the seam and C core (`ui_mouse_position` /
  `ui_keyboard_modifiers` / `ui_mouse_wheel_delta`) and every layer now agrees._ `mouse_wheel_delta(id)`
  keeps its name (the
  `_delta` carries the **Wheel Delta** glossary term) and `scroll_target(id)` keeps its name (a
  routing declaration, not a query). The just-shipped names churn now while there is
  only one consumer.

## Considered alternatives

- **Cooked element-scoped drag, mirroring the wheel** (`ui_drag_target(id)` +
  `ui_get_element_drag(id)` over the core's `active_id`/`is_dragging` latch). Rejected: it hands
  back a physical-pixel, cumulative-from-start `drag_delta` the app must re-baseline and un-scale;
  it is hover-anchored where a pan must survive the cursor leaving the rect; and it imports the
  whole Scroll-Target arbitration machinery to solve a reliability problem buttons do not have.
  The app owns its gesture machine anyway.
- **A single up/down state value** per button. Rejected: lossy — cannot represent a same-frame
  tap, and conflates the held *level* with the down/up *edges*. The `{ pressed, held, released }`
  shape (the core's own `ActionState`) is the fix.
- **A `{ pressed, held, released }` struct returned across the FFI from C.** Rejected: it would
  need a new mirrored type and drift guard for one app's need; three `bool` queries composed on
  the Rust side keep the seam mirror-free.
- **Keep the wheel's scoped model for symmetry.** Rejected: symmetry of *mechanism* would be
  cargo-culting. The wheel is scoped because it is zeroed/routed; buttons are not, so forcing them
  through arbitration adds surface for no correctness gain. The symmetry that matters is *naming*
  (`mouse_*` / `keyboard_*`), which this slice fixes.
- **Bind `FlSignal` for a plain region** to get `pressed`/`released`/`dragging`. Rejected for the
  same reason ADR-0021 did: those channels gate on `signal.hovered` (set only for
  `Clickable|Draggable`) and would read zero for a bare layout; and the app wants the raw primitive,
  not a per-widget signal.

## Consequences

- flowi's input vocabulary gains **Mouse Button State** and **Mouse Position** (CONTEXT.md
  `## Interaction`), recording the load-bearing asymmetry: the wheel is routed/scoped, buttons are
  global/unrouted, and the drag latch is the app's.
- The query namespace becomes coherent (`keyboard_*` / `mouse_*`); the two prior ergonomic names
  are renamed to `keyboard_modifiers` / `mouse_position` — a breaking change absorbed now because there
  is only one consumer so far.
- The `MouseButton` enum is extended to `X1`/`X2` to match the core `FlMouseButton`, closing a
  latent gap rather than forcing a second edit later.
- Raw global buttons mean the app, not the toolkit, disambiguates pan-vs-clip interaction (by
  button choice or its own hit-test). Acceptable while the app owns hit-testing; if a future
  widget-vs-canvas button conflict needs toolkit arbitration, it can be added behind a flag
  without disturbing the global primitive.
- The raw keyboard queries (`key_pressed/down/released`) were **deferred** when this was written and have
  since landed on the same global, unrouted model (`api/ui_input.def`). A window-absolute
  `mouse_position_window()` remains deferred — not needed by the pan slice.
