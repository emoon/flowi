# Detach/promotion is internal-floating-first with explicit compositor-placed OS-window promotion

## Status

accepted

## Context

Docking (ADR-0039) must let a panel leave its dock tree: float loose, or become its own OS
window — the latter is central to multi-monitor workflows (several views open across screens).
Dear ImGui's docking branch does this with **viewports**: dragging a panel to the desktop spawns an
OS window *at the cursor's global position* that then *glides under the mouse* as you drag.

That model depends on three things the windowing client must do: (a) position its own top-level
windows at chosen global coordinates, (b) track the global cursor across windows, (c) know where
its windows sit on the desktop. **Wayland — the primary development/runtime platform (Hyprland)
— denies a client all three by design.** X11, Windows, and macOS allow them. This is the
well-known reason imgui multi-viewport is degraded-to-broken on Wayland.

The flowi window backend (`FlWindowBackend`, `window_backend.h`) reflects this: `create_window` takes an *initial*
`x`/`y` (and already defaults to compositor placement), but there is **no** post-creation
`set_window_position` / `get_window_position` / `raise`, and **no** global-cursor query — the
event pump is demuxed strictly per window. Adding those for a viewport model would add API that
Wayland silently ignores.

## Decision

Adopt an **internal-floating-first** interaction now (option A), architected so a platform-tiered
live-viewport gesture (option C) is a purely additive follow-up.

- **Three panel states:** **docked** (in a tree leaf), **internal-floating** (a Clay floating
  element over a host workspace — the drag ghost, the drop preview, and "undocked but loose"
  panels all use this), and **promoted** (its own OS window hosting its own dock tree).
- **All in-window docking and the entire drag-dock interaction use internal Clay floating
  elements** — zero OS-window churn, fully headless-testable.
- **OS-window promotion is explicit and compositor-placed.** A panel becomes a real OS window
  only via an explicit action ("open in new window" / drag-tab-to-promote), and the **compositor
  places it** — flowi never chooses a global coordinate. Multi-monitor placement is then the
  user's window manager's job (e.g. Hyprland throws the promoted window to another screen).
- **Cross-window docking between two flowi windows** is supported via the per-window event pump's
  enter/leave; the "drag into empty desktop and the window follows the cursor" gesture is **not**
  part of A.
- **Persistence stores a promoted window's size but not its position** (ADR-0039), because
  Wayland cannot restore position anyway; the compositor re-places on reopen.
- **Option C (deferred):** the live tear-off-to-desktop viewport gesture may later be added for
  X11/Windows/macOS as an additive layer (needs `set/get_position` + global cursor on those
  backends), gracefully absent on Wayland. It does **not** change the ADR-0039 data model.

## Considered alternatives

- **Full imgui viewport model now (option B).** Rejected: it requires window position + global
  cursor that Wayland ignores, so it would be great on secondary platforms and broken on the
  primary one — and it pushes OS-window machinery into the core for a gesture Wayland can't honor.
- **OS-window-per-region** (every docked region is its own OS window). Rejected in ADR-0039's
  framing: heavy OS-window churn and it fights the Detachable-Panel transparency goal.
- **Promote-only, no internal floating** (a loose panel is always a real OS window). Rejected:
  every undock/redock would hit the OS window path (unavailable/janky on Wayland) and would not
  be headless-testable; internal floating keeps the common case in-process.

## Consequences

- The drag-dock interaction and drop preview are both in-window Clay — so the
  interaction's *decision* (hit-test → request) is a pure, headless-testable function; only
  promotion crosses the OS seam.
- The Window Backend stays minimal: no global positioning/cursor API is added for A. Option C
  would add it behind a platform-capability check, not in the neutral seam Wayland sees.
- Users on Wayland get explicit "open in new window" + WM-driven multi-monitor placement; users
  on X11/Win/macOS can later get the live tear-off without any model change.
- "Why doesn't flowi tear off windows like imgui?" is answered here: it is a deliberate
  Wayland-driven choice, deferred not declined.

## Amendment (2026-06-22): promotion implemented via an injected host seam; cross-window re-homing needs a carried-panel model

The **promoted** state above is now implemented (ADR-0041): an explicit action moves a panel's
subtree into a second workspace and the runner opens a compositor-placed OS window hosting it;
closing that window demotes the panel back. flowi owns the decision + lifecycle, the runner owns
the window mechanics through an injected `FlDockHost` vtable — flowi still chooses no global
position, so the WM tiles the window (the Wayland constraint this ADR is built around).

**Cross-window docking between two flowi windows** (line 41 above) turns out to need more than the
per-window enter/leave pump, and is split into a follow-up. On Wayland a held mouse button is an
*implicit pointer grab* on the originating surface: window B receives no `Enter` until the button
is released, so a single press-drag-release with a live compass/preview *in B* is impossible —
the same root cause this ADR cited for rejecting viewports. The follow-up adopts a **carried-panel
model**: a *click* lifts a tab (button released), the lifted panel becomes shared-tier state, the
pointer moves across windows so each window's own `Enter`/`Leave` fire, the hovered window draws
its own compass via `hit_test_drop_zones` against its own tree, and a second click commits the
re-home. (`FlMouseEvent_Enter`/`Leave` already exist.) This does not change the ADR-0039 data
model — it is still apply(tree, request) — only the *gesture* that produces the request.

## Amendment (2026-08-18): the overlay z-band table

Internal floating state above shares one z axis with every app/panel overlay, so the dock publishes
the bands that make cross-seam overlays predictable (the current table, without the removed 500 band,
is ADR-0064). They live as one internal define block
(`UI_Z_*` in `ui_private.h`); there is no public `FL_Z_*` and a panel author never names a z number.

| Band | Constant | Occupants |
|------|----------|-----------|
| 0 | `UI_Z_DOCK_CONTENT` | docked tiles, tab strips, docked panel bodies |
| 500 | `UI_Z_DOCK_LOOSE_PANEL` | internal-floating loose-panel placeholders |
| 1000 | `UI_Z_OVERLAY` | tooltip, menu, dropdown, modal backdrop, list drag chrome |
| 3000 | `UI_Z_DOCK_DRAG_CHROME` | drop-zone buttons, reserved-slot preview |

Equal z ties break on emission order — Clay's root sort swaps only on strictly-less, so it is
stable. The modal backdrop sits in the 1000 band rather than above it, so it never covers dock drag
chrome. An overlay is bounded by the window, never clamped to its tile or the workspace: a floating
element is a layout root of its own and escapes the panel clip in both attach modes.

The 500 band was transitional; the next amendment records its removal.

## Amendment (2026-08-18): internal floating is a transient holding pen, not a resting state

The **internal-floating** state above is no longer one of three states a panel can rest in. It is the
pen the drag gesture parks a panel in between the undock that starts the gesture and the commit or
cancel that ends it, and nothing else reaches it:

- Every exit from a drag lands the panel back in a leaf. A resolved drop commits it; Escape, a release
  outside the workspace, and a release over no zone all return it to the seat it was pulled from, or
  to the floor when that seat dissolved. The `floating[]` set is empty outside a gesture.
- Showing a **closed** panel — `fl_dock_set_panel_visible(id, true)` on one absent from the tree —
  re-homes it onto the root rather than placing it loose, through the same conservation-law safety net
  every dock op already falls back to.
- The loose-panel placeholder and the **500 z band above are deleted**. The drag overlay is the
  placeholder: one element in the 3000 drag-chrome band that follows the cursor and renders the
  dragged panel's **live content**, so exactly one thing draws that panel for the whole flight — the
  vacated slot never draws it too. The overlay is tinted by an opaque **mix**, not element opacity,
  which would let the tiles below bleed through the content it sits over.
- There is consequently no is-dragged predicate on any surface, and no reason for an app to draw a
  ghost of its own.

Option C (the live tear-off-to-desktop viewport gesture) is unaffected: it would add another exit from
the same holding pen, not a resting state.
