# Element state is pulled by id, in logical space

An app (a timeline viewport) needs to read a laid-out element's rect and its
cursor-over (`hot`) bit by id — the state the C core already kept per widget,
returned by `ui_get_widget_state`. We expose this to callers (and to the Rust bindings)
as **`element_state(id) -> ElementState`** / C `fl_input_get_element_state` → `FlElementState`,
renaming away "widget" across both languages: the query is keyed by an *element* id and
serves any laid-out element — a plain area as readily as a built-in widget — so "element"
is the noun that fits, and it avoids overloading "widget", which already names the
`Signal`-returning push result of a built-in control. The binding returns one safe record
covering the whole surface (rect, `found`/`visible`, `hot`/`focused`/`active`, and the
animation interpolators), with the rect normalised to **logical (authoring) pixels** — the
same space as `mouse_position` and the Painter — so size composes directly with cursor and
content-transform math and the app never needs the deliberately-unexposed `ui_scale`.

## Considered Options

- **Narrow per-facet accessors** (`element_size`, `hovered`, …) vs **one struct mirror.**
  Chose the mirror: the rule is "expose the whole supported surface, don't re-touch this
  file per feature", the C call already returns a struct in one hashmap lookup, and `found`
  disambiguates the zero-rect case honestly. (Sibling reads like `mouse_position` stay
  narrow because their C functions each return a single value.)
- **Physical px (faithful to C) vs logical px (normalised).** Chose logical: `ui_scale` is
  intentionally not exposed, so a physical-px rect would be unconvertible and would put the
  app in two coordinate spaces — a silent off-by-scale bug that only appears at non-100%
  DPI. `mouse_position` already normalises for exactly this reason.
- **Keep `WidgetState` (match C name) vs rename to `ElementState`.** Renamed both sides
  (~36 references across 8 owned files, no submodule). Keeping "widget" would overload it
  to mean two different result types (`Signal` and the state record); "element" is also the
  authoring vocabulary (`col!`/`row!`/`layout!`) and the glossary's generic node noun.

## Consequences

- The internal `ItemState` / `item_state` storage keeps the "item" noun — it is never
  user-facing, so it is out of scope for the rename.
- `focused`/`active` and the animation interpolators are populated by the Widget Prologue;
  a plain non-widget element reports its rect/`visible`/`hot`/`found` and zeros for those.
- A plain area does not enter `item_state` on its own — a widget enters via its behavior, but
  a bare container must opt in by declaring itself a scroll target (the timeline-viewport
  pattern) or, internally, via `ui_track_element` (registers it without the wheel flag); until it does, `found` is false. The C-side rect stays physical px (its existing
  consumers depend on that); only the Rust binding normalises to logical.
