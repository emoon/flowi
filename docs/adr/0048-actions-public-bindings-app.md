# Semantic actions are public (`FlAction`); bindings stay an internal, app-level mechanism

## Status

accepted

## Context

flowi has an internal action system (`flowi/src/ui/input_actions.{h,c}`) that decouples *physical*
input (keys, mouse buttons, gamepad buttons/axes) from *logical* actions. The enum (originally the
internal `UiAction`, now the public `FlAction`) names the logical verbs — `FlAction_Confirm`,
`FlAction_Cancel`, the directional `FlAction_Nav_*` set, plus widget/text-editing actions — and a
per-action table of `InputBinding` records (~26 `static const` tables,
`s_default_bindings[FlAction_Count]`) maps physical inputs onto them. Widgets query the system
internally via `ui_action_pressed(FlUiCtx*, FlAction)`.

The public nav/action query API (the dependent slice that follows this one) needs to name actions on
the `fl_*` surface — a controller/keyboard-driven downstream asks "was *Confirm* pressed", "is *nav up*
pending". So `UiAction` had to become a public IDL enum. That forces a boundary question, because the
action system has two halves that are easy to conflate:

- the **semantic vocabulary** (what *Confirm* / *Cancel* / *nav up* mean), and
- the **binding** (which physical input triggers each — the mapping table).

Grounding the split in the actual consumer (an embedding host, flowi's downstream) is decisive. Its
code queries semantic actions (`UI_ACTION_CONFIRM`, `UI_ACTION_CANCEL`, the pre-rename names), the nav helpers
(`nav_up_pressed`, `consume_nav_*`), and focus (`set_focus`, `get_nav_frame`, `focused_id`). It
**never names a binding type** — no `InputBinding`, `InputType`, or modifier reference anywhere, and it
never reads or mutates the binding tables. The binding layer is already invisible to the consumer; only
the vocabulary crosses the boundary.

This boundary is the toolkit→`fl_*` / app→host razor: the semantic vocabulary is toolkit, the physical
mapping is app-level input configuration. And it is hard to reverse — `FlAction` becomes a downstream
plugin-ABI type, so its layout is frozen per ADR-0043.

## Decision

Split the action system along the vocabulary/mechanism seam:

- **`FlAction` is public** — convert the `UiAction` enum **1:1** to the public IDL enum `FlAction`
  (all members, verbatim mapping; `UI_ACTION_*` → `FlAction_*`, e.g. `FlAction_Confirm`,
  `FlAction_Nav_Up`). This was a mechanical rename; no `UI_ACTION_*` references remain. The enum
  is the **stable semantic core**; per ADR-0043 it is layout-frozen plugin ABI for any downstream
  that exposes it.

  The enum is kept whole rather than split into a public "nav/confirm" subset plus an internal
  "text-editing" enum: it is a single parameter vocabulary backing one `actions[FlAction_Count]`
  machinery, so splitting would mean two parallel enums and a mapping for no surface reduction. The
  text-editing / drag / scroll members are public in the enum but are internal-use — no public query
  function meaningfully needs them; a doc note marks them as not a supported external query rather than
  carving them out of the type.

- **Bindings stay internal and app-level** — `InputBinding`, `InputType`, the modifier enum,
  `check_binding`, `ActionSystemState`, and the default binding tables keep their internal
  (`Ui*`/no-prefix) form and **do not move**. flowi keeps shipping its default bindings internally so
  widgets work standalone. "Bindings are app-level" means **remapping is not exposed**: there is no
  public `fl_*` to mutate which input triggers an action; a downstream that wants custom controls drives
  its own input layer. Nothing about the binding mechanism is promoted in this slice.

This slice introduced only the enum on the public surface. The query functions
(`fl_action_pressed(FlAction)`, `fl_nav_*_pressed`, `fl_consume_nav_*`, `fl_set_focus`,
`fl_get_nav_frame`; `flowi/include/flowi/ui/nav.h`) followed in the dependent slice, layered on this
enum.

## Considered alternatives

- **Public `FlAction` as a nav/confirm subset; text-editing/drag stay an internal enum.** Rejected: the
  machinery indexes a single `actions[COUNT]` array by one enum; a subset forces two enums plus a
  superset/subset mapping — more code and a fragile index contract — to shrink a *parameter* vocabulary
  that costs nothing extra to carry whole. ADR-0043's freeze bites only on what a downstream actually
  exposes, so unqueried members carry no real ABI weight.
- **Promote bindings to the public surface too (public `InputBinding` + a remapping API).** Rejected:
  the consumer never touches bindings, so it is speculative surface; and it would freeze the binding
  struct layout as plugin ABI for a remapping feature no one is asking for (the header states bindings are
  hardcoded defaults with no remapping).
- **Physically move the default binding tables out of flowi to the app.** Rejected: flowi's own widgets
  (text input, menu, virtual keyboard) depend on the default bindings to function bare; relocating them
  would force the host to inject a binding table at `ui_create` and break standalone widget behavior —
  far beyond this slice's mechanical-rename scope, for no consumer benefit.

## Consequences

- `FlAction` is a public, layout-frozen IDL enum; downstream controller/keyboard UIs name semantic
  actions on the `fl_*` surface, and the dependent query-function slice can build on it.
- The binding mechanism is a known **internal** boundary: changing binding tables or `InputBinding` is a
  routine internal edit, never an ABI concern, because no public type or function exposes it.
- Remapping remains a future, app-side concern. If flowi ever exposes binding configuration, that is a
  new, separately-justified surface — not implied by this ADR.
- A future widening of `FlAction` (new members) is an enum-width ABI change under ADR-0043 for any
  downstream that exposes it, requiring a coordinated plugin rebuild — so new actions are added
  deliberately, not casually.
