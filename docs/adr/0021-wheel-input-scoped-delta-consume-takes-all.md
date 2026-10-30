# Wheel input is a scoped per-frame delta, routed consume-takes-all to the hot scroll target

## Status

accepted

## Context

An application needs to read the mouse wheel over a plain `layout {}` region to drive
its own timeline navigation (vertical scroll, Shift+wheel pan, Ctrl/⌘+wheel zoom). It owns its
view offset and zoom and deliberately rejects the engine-managed scroll area, so it needs flowi
only to *expose the wheel over a region* — not to manage scrolling.

The originating request framed this as a binary: either a zero/simple-config wrap of the
existing `FlSignal` makes a bare layout element report wheel/drag (a *binding gap*), or it
cannot and the slice re-scopes (a *capability gap*). Reading the code, neither held:

- `ui_widget_behavior(id, cfg)` hardcodes `STANDARD_WIDGET_FLAGS`
  (`Clickable | Focusable | + animations`) — no `ScrollTarget`, no `Draggable`. In
  `ui_item_behavior` (`ui/ui.c`), `wheel_delta` only populates under `flags & FlItemFlags_scroll_target`
  **and only when `signal.hovered` is set — which is itself only assigned under
  `flags & (FlItemFlags_clickable | FlItemFlags_draggable)`**; `drag_delta` only under
  `flags & FlItemFlags_draggable`. So the proposed binding target can *never* report wheel or drag, and
  a pure `ScrollTarget` element reads zero through the `FlSignal` path (existing scroll-target
  widgets all co-flag `Clickable`, e.g. the slider, which is why this never bit anyone). The flags
  are a separate `FlItemFlags` argument, not fields of `FlWidgetConfig`, so no config value enables
  them. The `button_regular` zero-config precedent does not transfer.
- The capability fully exists — `ui_item_behavior(id, FlItemFlags_scroll_target | …, cfg)` populates
  everything. So it is a binding gap, but against a *flag-bearing* entry point, not
  `ui_widget_behavior`.

A second discovery decided the shape. `ui_update_begin` (`ui/ui.c`) routes the wheel to
`Clay_UpdateScrollContainers` for any region the cursor is **not** over a declared `ScrollTarget`,
and then **zeroes the cached wheel unconditionally** — even when no scroll container actually
consumed it. So a plain layout that never declares itself a scroll target reads zero. Reliable
wheel reads therefore *require* an element to opt into wheel arbitration; this is not ceremony,
it is how an inner scrollable shadows an outer one (the topmost `hot` target wins).

## Decision

Wheel input is modeled as **routing + a scoped delta read**, kept distinct from the widget
`FlSignal`.

- **Opt-in by a flowi-owned registration.** An area or widget declares itself a **Scroll
  Target**, which registers the element with `FlItemFlags_scroll_target` so it enters wheel arbitration.
  This is *new plumbing*: a plain `layout {}` area never enters `item_state` today (only
  `ui_item_behavior` creates entries via `get_or_create_key`, and `item_state_update` skips ids
  not already present), so the registration must insert the area into `item_state` and set the
  flag, after which the existing `hot`/`next_hot_id` tracking picks it up for free. The
  registration lives in **flowi-owned** code (a dedicated entry point, `fl_input_scroll_target(id)`), **not** as a new field on
  the vtable-crossing `FlLayoutConfig`/`FlWidgetConfig` in the public layout header — those
  are shared across other trees and adding a field shifts offsets everywhere. The bindings may
  still *surface* it as a declarative flag (`.scroll_target()`); that ergonomic flag is a Rust-layer
  concern, decoupled from the C representation.
- **Consume-takes-all routing.** Each frame's wheel goes to the single topmost Scroll Target
  under the cursor — the `hot` element — and to no one else. An inner target shadows its
  ancestors. The wheel does **not** bubble to a parent the inner target declined to use.
- **Scoped delta read.** The value is read by `fl_input_mouse_wheel_delta(id)` — a **new standalone query
  that reads `item->state.hot` directly**, returned only when `id` is the hot Scroll Target. It
  must *not* be routed through `ui_item_behavior`'s `signal.wheel_delta`, which gates on
  `signal.hovered` (set only for `Clickable|Draggable`) and would silently read zero for a pure
  Scroll Target. It is a **per-frame delta** (events accumulated that frame, reset to zero each
  frame); there is **no absolute wheel value** — any cumulative offset is the app's to accumulate
  (the app already owns `scroll_x`/`scroll_y`). This is symmetric with
  `fl_input_mouse_position(id)`. It coexists with the pre-existing global `fl_input_mouse_wheel()`
  (unscoped, unbound); the scoped query is the binding such an app consumes.
- **No `FlSignal`, no `FlWidgetConfig` bind, for this slice.** Drag stays unbound until the
  clip-editing slice, where the press-capture state machine (which the `FlSignal` path owns)
  earns its place. Cursor and modifiers stay as free queries:
  `fl_input_mouse_position(id)` (logical, scale-correct) and `fl_input_keyboard_modifiers()`. Shift+wheel
  is axis-swapped at the input substrate, so a modifier query is needed only to disambiguate
  same-axis gestures (plain scroll vs Ctrl+wheel zoom).
- **Drop the unconditional cache-clear** in `ui_update_begin`. It is safe to remove because the
  scoped read self-gates to one reader, and it was over-aggressive regardless (it cleared even
  when nothing consumed the wheel).

## Considered alternatives

- **Bind `ui_widget_behavior` / a zero-config `FlSignal` wrap** (the request's assumption).
  Rejected: it rides `STANDARD_WIDGET_FLAGS` and can never report wheel/drag; the flags are not
  in `FlWidgetConfig`.
- **Return the wheel on an `FlSignal` from a behavior call** (like every other widget).
  Rejected for this slice: the navigation gestures are wheel + cursor + modifiers — all
  stateless reads — so a bundled `Signal` plus exposed flags is more surface than the job needs.
  Cursor and modifiers are already free queries; making wheel one too is consistent. The Signal
  path returns when drag (stateful capture) is actually needed.
- **A field on `FlLayoutConfig`/`FlWidgetConfig`** to carry the flag. Rejected:
  those structs live in the public layout header, are shared across other trees, and
  cross the plugin vtable by value — adding a field shifts offsets everywhere and forces a mirror
  + drift-guard regen for one app's interaction need. The chosen flowi-owned registration keeps
  the plugin ABI untouched while still allowing a declarative `.scroll_target()` flag at the
  Rust layer.
- **An absolute/accumulated wheel value.** Rejected: the hardware emits deltas; there is nothing
  to be absolute to. The cumulative offset is app state, not framework state.
- **Bubble-if-unused routing.** Rejected for now: in immediate mode "did the inner use it?" is
  only knowable after the fact, requiring a deferred consume protocol. Consume-takes-all matches
  mainstream GUIs; bubbling can be added later behind the same flag.
- **Relax only the cache-clear so single regions need no flag.** Rejected as the *whole*
  solution: it helps the single-region case but evaporates the moment a second scroll region
  exists (an application with several panels, e.g. mixer/browser/rack), which still needs the flag for arbitration. The clear is
  dropped, but the flag is the durable primitive.

## Consequences

- flowi gains its first interaction vocabulary (CONTEXT.md `## Interaction`: **Scroll Target**,
  **Wheel Delta**), distinct from the rendering language the file began as.
- The binding slice is small: a config flag + `fl_input_mouse_wheel_delta(id)` + the existing
  `fl_input_keyboard_modifiers()` / `fl_input_mouse_position(id)` free queries.
- Headless wheel injection was **deferred** when this was written; it has since landed as `fl_app_inject_wheel`.
  The consume-takes-all routing is proven by C unit tests (`unit_tests_widgets.c`) and an end-to-end case that
  drives the wheel through `fl_app_inject_wheel` (`unit_tests_render.c`).
- The unconditional cache-clear removal is a behavior change to shared scroll routing; it is
  tested independently of the binding.
- Bubbling (the request's Q6) and drag remain open hooks behind the same Scroll Target flag.
