# Text overflow is a core stateless text-config mode; marquee is a deferred stateful widget

A single-line text run that overflows its box today only hard-clips (the remainder is scissored
away). Adding **eliding** (truncate to the longest fitting prefix + `…`) raised a classification
fork: is overflow handling app-side policy, or a core concern? We decided it is a **stateless
display mode on the core text config** — an `overflow` enum on `FlTextConfig` beside the existing
`no_wrap`, with `Clip` (the default, == today's behaviour) and `Elide`, implemented in the layout
engine's existing post-layout text-wrap pass. Selecting `Elide` **implies** the box width
constraint (a grow-to-content run has nothing to elide against). Animated **marquee** scrolling
(wrap-around / ping-pong) is explicitly **not** an overflow mode and is deferred to a separate
stateful text widget.

## Considered Options

- **App-side eliding over a bound text-measurement query.** Promote the internal
  `font_cache_get_text_size` to a public `ui_get_text_size`, bind it, and let each app measure +
  truncate + append `…` itself (caching the result). This was the initial recommendation, on the
  project's recurring "app owns policy" lean. **Rejected:** that principle governs *interaction /
  view-state* (drag latching, scroll accumulation, key routing), not text *display* — wrapping,
  valign, and letter-spacing already live in the core text config, and eliding is the same
  category. App-side eliding also only works where the app already knows the box width (true for a
  timeline clip computed from the content transform, **false** for buttons, menu items, list rows, and
  any grow/fit container, where the width is only known after layout). And every consumer would
  reimplement the same truncate-and-cache logic plus the font-not-ready retry by hand.

- **Eliding in the core (chosen).** The layout engine already runs a *post-layout* text pass that
  fits text to the final box width (it is where wrapping happens and where today's hard-clip is
  produced — the overlong single line is emitted and the parent clips it). An elide branch slots
  in at that same fits-check: when the run does not fit and the mode is `Elide`, truncate to the
  prefix that fits in `box_width − ellipsis_width` and append `…`. **Render-when-ready falls out
  for free** (ADR-0024): a font that is not metrics-ready measures `(0,0)`, so no elide happens
  that frame and it retries next frame — the core never blocks and the app never touches the async
  trap. Eliding does not depend on a public measurement query (`Text::measure` has since been added as a
  separate call for sizing painter text); the binding is just a new plain enum
  field flowing through existing api_gen (not the ADR-0026 bitflags path), named per ADR-0027.

- **Marquee (`WrappingScroll` / `PingPongScroll`) as further enum values.** **Rejected — a leaky
  abstraction.** `Clip`/`Elide` are stateless pure functions of `(text, width, font)` recomputed
  each frame with zero per-element memory. A marquee is **stateful and time-driven**: it needs a
  per-element scroll offset advanced over time (and a direction + end-pauses for ping-pong),
  forcing continuous redraw, plus a pile of tuning knobs (speed, pause, gap, start delay,
  hover-only) that would bloat the by-value text config every `ui_text` call carries. Crucially,
  `ui_text_styled` / `Painter::text` is a stateless anonymous emit with no id and no `ItemState`
  slot to hang an offset on — animating it first requires giving text-in-a-box a stable identity,
  i.e. a widget. The codebase already shows that shape: `ui_text_input` scrolls text horizontally
  via widget-owned state (`scroll_offset_x` / `target_scroll_x`), not a config flag. So marquee is
  a separate, later, stateful widget keyed by id — not a fourth value in this enum.

## Consequences

- `FlTextConfig` gains an `overflow` enum; `Clip` is the default so no existing call site changes
  behaviour. The eliding logic lives in the layout engine's text-wrap pass, not in app code.
- A future reader sees overflow handled in the core *despite* the project's app-owns-policy lean;
  the dividing line recorded here is **stateless display = core, stateful animation = widget**.
- Marquee is intentionally absent from the enum. When built, it must be an id-keyed stateful
  widget (the `ui_text_input` precedent), and is specified in ADR-0069.
- Eliding starts end-only with a literal `…`; middle-elide, if ever needed, is a new enum variant,
  not a reshaping of this contract.
