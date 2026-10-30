# Marquee is an id-keyed stateful widget: own state map, embedded text config force-clipped, mandatory sizing

Status: accepted as a design; not yet implemented - no `fl_marquee` exists in `api/` or the generated headers.

ADR-0065 deferred animated text scrolling (marquee) out of the stateless `overflow` enum and
prescribed only its category: an id-keyed stateful widget. This ADR records the widget's actual
contract, decided in the design pass for the marquee widget: a single public entry
`fl_marquee(id, text, config) -> FlSignal`, where the id is stable and **separate from the text**
(the text is display payload; a text change resets the motion — a new title is read from its
beginning), the config embeds the full `FlTextConfig` with `overflow` **force-overridden to
`Clip`** and `no_wrap` forced on, and the caller must make an **explicit sizing choice** (fixed
logical-px width or grow-to-parent) — a zero-initialized sizing field debug-asserts rather than
producing a widget that silently never scrolls.

## Considered Options

- **Text as the id (`fl_marquee(text, ...)`), matching `fl_button(label)`.** Rejected: a marquee's
  text changes while the widget stays put (a now-playing display swaps titles constantly), so
  text-as-id leaks stale state per unique string and collides when two marquees show the same
  string. Separate id + reset-on-text-change gives the wanted UX (new track ⇒ scroll restarts)
  without either failure.

- **State on `ItemState` (ADR-0065's literal wording) or caller-owned like `FlTextInputState`.**
  Rejected both: adding offset/phase/pause fields to `ItemState` taxes every interactive element
  in the app for a rarely-used widget, and a caller-owned struct burdens every call site of a
  fire-and-forget display widget. Chosen: a private `MarqueeState` in its **own id-keyed hashmap**
  on the shared UI tier with `last_frame` staleness eviction — the existing `scroll_states`
  pattern, surviving dock promote/demote like the other shared maps.

- **Flat style fields instead of embedding `FlTextConfig`.** Rejected: re-declared styling drifts
  from the text config as it grows (letter-spacing, valign, …). Embedding wins, but creates one
  incoherent combination — `overflow = Elide` inside a marquee — which the widget silently
  overrides to `Clip` (documented caller-facing per ADR-0044) rather than asserting, since a
  zero-init struct must stay valid and the call has no error channel.

- **Default sizing (grow-x) instead of a mandatory choice.** Rejected in review: a marquee is only
  meaningful against a bounded width (the mirror of ADR-0065's "elide implies the width
  constraint"), and an unconstrained fit-to-content box degrades it to permanently-static text —
  a silent bug. But fixed-px-only was also rejected (in resizable dock panels the width is only
  known after layout — the same reason ADR-0065 rejected app-side eliding). The contract is an
  explicit **choice** between fixed width and grow; only the absence of a choice is the error.

- **`void` return.** Rejected: the widget already owns a stable id and interaction state, so a
  `FlSignal` (hover/click — e.g. a clickable now-playing title) is free now, whereas adding it
  later reshapes a generated signature that is downstream plugin ABI (ADR-0043).

## Consequences

- Motion contract: a fixed phase cycle (start delay → constant-speed scroll → end pause → wrap or
  reverse), speed stated in logical px/sec (scale-independent perceived speed, ADR-0020), leftward
  only in v1 (direction is a future additive knob, like ADR-0065's middle-elide). Wrapping mode
  re-enters after a gap by emitting the run twice inside the existing clipped
  `manual_scroll` layout area (the `ui_text_input` mechanism); ping-pong emits it once and pauses
  at both ends. Constant velocity is new `offset += speed * dt` logic — the existing `anim_*`
  helpers are ease-toward-target and the text-input `0.3` lerp is not frame-rate aware; neither is
  reused for the cruise.
- Fitting text — including the not-yet-metrics-ready frame, which measures (0,0) per ADR-0024 —
  renders as static text at rest with the phase re-armed; the widget never animates fully visible
  text.
- A `trigger` knob (`Always`, default | `Hover`) is the whole hover surface in v1; hover-out in
  `Hover` mode snaps the offset back to zero. Pause-on-hover is deliberately absent (freezing
  mid-scroll strands the reader mid-string) and can be added without breaking the enum.
- The marquee relies on the `fl_app` runner's unconditional per-tick redraw; no request-redraw
  mechanism exists or is added.
