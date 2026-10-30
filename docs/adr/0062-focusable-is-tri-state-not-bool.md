# `FlWidgetConfig.focusable` is tri-state, not a bool

Status: accepted

## Context

`FlWidgetConfig.focusable` was a `bool`, documented as "whether the widget can receive focus".
It did not work. `STANDARD_WIDGET_FLAGS` carries `FlItemFlags_focusable` for every widget that
goes through the widget prologue, and `ui_item_behavior` registered focus on that flag alone
without ever consulting the config. A widget declaring `focusable: false` still joined
`focus_order`, where the auto-restore that runs when nothing holds focus hands the frame's first
entry the ring — so it took focus it could not use and stroked a focus border, and nothing could
move off it, because the tab strip gates its own arrow-key handling on the same field it had
passed as false.

Making the field load-bearing is not as simple as reading it, because the whole config API is
built on a zero default: `(FlWidgetConfig){0}` in C, `..Default::default()` in Rust, with api_gen
asserting every by-value enum field admits the all-zero bit pattern so `core::mem::zeroed()` is a
valid `Default`. Against a `bool`, "unset" and "explicitly false" are the same bit. Honoring
`focusable: false` therefore *necessarily* means every widget whose caller never named the field —
button, link, checkbox, switch, slider, radio, tab bar, dropdown trigger, and every zero-config
call in `examples/` — silently leaves the tab order. A slider's whole arrow-key adjustment path is
gated on `signal.focused`, so it would go dead. That is a library-wide keyboard-accessibility
regression traded for a niche opt-out.

The alternative considered was to leave the field a bool and have each widget family force the bit
on where focus is non-negotiable (text input first, then slider, then checkbox…). That collapses:
once every family that needs keyboard access forces it, the field means nothing anywhere, and which
families honour it becomes arbitrary. The dropdown already showed the smell — its trigger passed the
caller's value through while its own search input and cancel row hardcoded `true`.

## Decision

`focusable` becomes a three-valued `FlFocusable` enum, single-sourced from
`api/widget_config.def`:

- `FlFocusable_Default` (0) — keep the widget family's normal focus behaviour. This is what a zeroed
  config carries, so callers that never name the field are unaffected.
- `FlFocusable_Yes` (1) — opt a family that is *not* focusable by default into the focus order.
- `FlFocusable_No` (2) — keep the widget out of the focus order entirely: skipped by tab and spatial
  navigation, never auto-restored onto, never strokes a focus border.

The zero value means "unset", which is the property a bool could not express and the reason the type
is three-valued rather than two.

The gate in `ui_item_behavior` is **subtractive only**: `No` masks `FlItemFlags_focusable` off,
`Default` and `Yes` leave the flag exactly as the widget family set it. `Yes` is deliberately not
honoured centrally — a family that is not focusable by default adds the bit itself, because only it
knows what else focus costs it (the scrollbar pairs it with `FlItemFlags_focus_animation`). Today the
scrollbar is the only such family.

Consequences:

- `FlWidgetConfig` grows from 48 to 56 bytes and field offsets after `child_padding` shift. The
  generated C header, both Rust modules, and the generated ABI probe move together from the one def.
- Source-breaking for callers that wrote `.focusable = true` / `false`. In-tree callers migrate to
  `FlFocusable_Yes` / `FlFocusable_No`. `true` happens to equal `Yes` numerically, so a missed C
  callsite keeps working; `false` equalling `Default` rather than `No` is the case to watch.
- Any surviving truthiness test (`if (cfg.widget.focusable)`) now reads an explicit opt-out as
  focusable, since `No` is 2. The two conditional sites were migrated to explicit comparisons; the
  compiler cannot catch a missed one.
- A zero-config tab bar gains working arrow-key navigation, which the bool default left switched off
  while the tabs were focusable anyway. This is the inconsistency described in the Context above, and
  fixing it is the point rather than a side effect.
- `ui_is_widget_visible` / `ui_scroll_to_widget` read `widget_scroll_map`, whose registration used to
  sit inside the focus branch. It now keys on scroll containment, since whether a widget is clipped by
  a viewport has nothing to do with whether it can take focus.
