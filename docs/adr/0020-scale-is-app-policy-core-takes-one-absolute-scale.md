# Scaling is app policy: the core takes one absolute scale, the app composes it

## Status

accepted

## Context

ADR-0010 chose the coordinate-space *model* (UI authored in logical pixels, a bigger
window yields more content, fixed design resolution rejected). It did not say where the
single UI scale is *computed*, and the code never matched it: the core baked in a fixed
1080p reference. `ui.c` called `ui_apply_scale_preset(.../1080)` so that
`ui_scale = window_height / 1080`, and the fl_app runner deliberately fed it the physical
pixel height and skipped DPI (`app/app.c`, "no separate window_dpi_scale factor, which
would double-count"). The net effect was the *rejected* scale-to-fit model: a bigger
window showed the same UI bigger, not more content. The bug was dormant only because most
work happens near 1080p where `ui_scale ≈ 1`.

That 1080p-reference behavior is **correct for the embedding host** — a fullscreen TV/console
app that must degrade to PAL/720p with a text-readability boost at low resolutions — and
**wrong for a desktop application**, which wants more arrangement on a larger monitor. The mistake
was not the model; it was hardwiring one app's policy into the framework, which is weird
for a generic UI library.

A `text_scale_factor` (the low-resolution text boost) and a `ui_set_dpi_scale` seam also
lived in the core. Both are facets of the same baked-in policy: the boost is a PAL
readability concern, and `ui_set_dpi_scale` only meant "multiply DPI onto the baked 1080p
scale."

## Decision

The core holds **zero scaling policy**. It stores one **absolute** UI scale and one
absolute text scale, both defaulting to identity, and multiplies authored logical pixels by
them. It computes nothing and queries nothing (it has no window handle).

- **`void ui_set_scale(float ui_scale, float text_scale)`** (internal, `ui/ui.h`) — the entire scale vocabulary
  of the core. Callable any time (DPI changes when a window moves monitors; resolution
  changes on a PAL switch); recomposes on call. Default `(1.0, 1.0)`. A minimal embedder
  calls it never and gets logical == physical.
- **`text_scale_factor` and `ui_set_dpi_scale` are deleted from the core.** Two floats in
  is the whole contract. Two scales survive (not one) only because the host's PAL boost needs
  text to scale independently of chrome — a real shipping requirement.
- **`ui_set_display_resolution(w, h)` sets the layout area only** — no scale side effect.
  Because the layout area is physical pixels and authored values are multiplied by the
  supplied scale, the *same core code* produces both behaviors purely from the scale number:
  a desktop app passing `dpi` gets more content on a bigger window; a TV host passing `height/1080`
  gets scale-to-fit. There is no mode flag.
- **Composition is the app layer's job.** fl_app (the desktop runner) reads
  `get_content_scale()` (monitor DPI) and a runtime UI-zoom preference
  (`fl_app_set_ui_zoom`, a relative multiplier where `1.0` is DPI-correct) and pushes their
  **product** to `ui_set_scale` whenever either changes or the window resizes. This is the
  home for ADR-0010's `monitor DPI × user UI-zoom`.
- **Reference-fit / PAL is an opt-in battery, not a core concept.** `ui/ui_scale.h` is
  renamed `ui/reference_fit.h` and exposes
  `UiScalePair ui_reference_fit(int height)` (returns `{ui_scale, text_scale}` from the
  1080p reference plus the PAL text boost). The core never references it. The host's migration
  is one include + `ui_reference_fit(h) → ui_set_scale(...)` on each resolution change. It
  stays in-tree as a reusable policy for any fullscreen/TV/console app.

## Considered alternatives

- **Core composes two factors** (`ui_set_dpi_scale` + `ui_set_ui_zoom`, multiplied in the
  core). Rejected: it puts the DPI×zoom policy back inside the core we are emptying out. A
  bare embedder who wants zoom multiplies it into their own `ui_set_scale` call; fl_app
  provides the convenience composition for its users.
- **A named scale-policy enum in the core** (`FIT_REFERENCE{1920,1080}` vs `LOGICAL_DPI`).
  Rejected: it enshrines the host's TV policy in the core forever — the "encoded base" that
  prompted this work. The behavior difference belongs entirely in the float the app supplies.
- **Move `ui_reference_fit` out into the host's repo.** Rejected: it is a reusable policy and
  keeping it in-tree holds the host's migration to one line, while the core still never
  references it.
- **Zero-touch host** (1080p stays the core default, opt *out* via an explicit scale).
  Rejected: it does not remove the baked policy, only demotes it to a default, leaving the
  reference logic and its `#include` inside the core. We chose the one-line, compile-visible
  migration over a silent default.

## Consequences

- The core is policy-free: two absolute floats, no DPI/PAL/zoom concepts.
- fl_app applies HiDPI automatically and gains a runtime UI-zoom override
  (`fl_app_set_ui_zoom`); a desktop app gets more content on a bigger monitor for free —
  *if* its main content area uses GROW sizing rather than fixed widths. Fixed-size
  panels sit in a bigger empty window. That is an app-side authoring concern, not a framework
  one.
- The embedding host keeps byte-identical behavior via one include + one call; no PAL/text
  regression.
- Tests migrate to `ui_set_scale`; the 540 case is re-pointed through `ui_reference_fit`
  so it regression-tests the helper, where that behavior legitimately lives (`unit_tests_render.c`).
  The 1080 cases are unchanged (old `scale=1.0` == new default).
- Builds on ADR-0010 (coordinate-space model); this ADR fixes *where the scale is owned*.
