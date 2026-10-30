# Overlay z bands

Status: accepted

## Context

Every floating element flowi emits carries a `z_index`, and Clay sorts tree roots by it before it
walks them into render commands. Without a stated ordering an app author has no way to answer "will
my overlay draw over a docked panel, and will a dock drag ghost draw over mine?" — so authors read
the answer out of private headers and copy the literal into their own code.

## Decision

The ordering is three internal bands in `flowi/src/ui/ui_private.h`:

| Band | Value | What emits in it |
| --- | --- | --- |
| `UI_Z_DOCK_CONTENT` | 0 | docked tiles, their tab strips, docked panel bodies |
| `UI_Z_OVERLAY` | 1000 | tooltip, menu, menu bar, dropdown, modal backdrop, list drag chrome |
| `UI_Z_DOCK_DRAG_CHROME` | 3000 | the dock drag ghost: fill, border, and the title it carries |

Ties break on **emission order**: Clay's root sort swaps only on strictly-less, so equal-z roots keep
the order they were emitted in. Two constructs stack inside the overlay band rather than rely on
that: the list's drag line and ghost take `UI_Z_OVERLAY + 1` and `+ 2` so they sit above the
marquee, and each submenu panel takes its parent's z plus its nesting level. The text-input caret is
the one element outside the table entirely, at a fixed 10 attached to its own input.

A modal covers an overlay only because its backdrop is emitted after it. Two overlays opened in the
same frame at the same band draw in emission order, not in open order.

### The bands stay internal

No `FL_Z_*` on the C surface, no named constant on the Rust surface. Exporting them makes the numbers
API: they freeze, apps do arithmetic on them, and the ordering stops being decidable in one place —
which is what collapsing the scattered per-widget `UI_*_DEFAULT_Z` values into this table was for. A
panel author is owed a documented ordering, not a symbol to pass around.

### `z_index == 0` means two different things

`menu`, `menu_bar`, `tooltip` and `message_box` read `0` as "the overlay band" and substitute 1000
themselves, so an author who omits `z_index` gets an overlay.

`floating` and `popup` pass `0` straight through. For `floating` that is deliberate: `0` is a real
band, because docked panel bodies are emitted at `UI_Z_DOCK_CONTENT`, which *is* zero. For `popup` it
is not deliberate — every in-tree caller sets `z_index` explicitly, so the pass-through has never
shown, and `PopupConfig`'s docstring still claims a default of 100 that no code implements.

So the rule an author learns from the menu docs does not transfer. An omitted `z_index` on a `menu`
is an overlay; on a `floating` or a `popup` it is dock-content level.

### What a panel author should do

Reach for the widget that owns its own floating element — `menu`, `tooltip`, `message_box`, or a
`popup` with an explicit `z_index`. Those are the supported path and they never require a number.

Raw `floating` needing to clear docked chrome is the one case with no symbol to reach for: pass a
literal at or above 1000 and below 3000, so it sits above dock content and still lets a drag ghost
cover it. Anything at 3000 or higher will draw over the dock drag ghost.

Consequences:

- The table above is the contract; the values in `ui_private.h` are the implementation of it. Moving a
  number means updating both.
- `popup`'s `z_index == 0` behaviour is documented as it is, not as `PopupConfig` describes it.
  Reconciling the two is a code change and is not made here.
