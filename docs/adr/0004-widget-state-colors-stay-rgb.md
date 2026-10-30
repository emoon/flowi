# Widget state colors stay RGB; the OKLCH module is removed

## Status

superseded by ADR-0015

## Context

flowi shipped `ui/ui_color_oklch.c/.h` — a perceptually-uniform OKLCH/OKLab
color space with `color_derive_hover/press/focus` helpers, intended to derive
interactive state colors. It was never wired in: theme derivation
(`vendor/base .../ui_theme.h`: "lighten base by 15%") and runtime state
application (`ui_apply_state_color` -> `ui_color_lighten/darken`, an RGB lerp
toward white/black) both use plain RGB math. The OKLCH module — conversions and
derive helpers alike — had zero callers while being compiled (~286 lines). A
golden full-buffer pixel regression harness now pins rendered output, so any
change to color math moves real pixels.

## Decision

Keep RGB-based state coloring. Remove `ui_color_oklch.c/.h` entirely. Consolidate
the per-widget color prologue into `fl_resolve_widget_colors` with byte-identical
output. Adopting perceptual (OKLCH) color is deferred to a separate, deliberate
change that rebaselines the golden harness.

## Considered alternatives

- **Wire OKLCH in now.** Rejected: changes every interactive pixel, forces a
  golden rebaseline, has no shipping need, and folding a visual change into a
  dedup refactor hides the pixel delta.
- **Keep OKLCH dead in-tree as "future" scaffolding.** Rejected: ~286 lines of
  compiled-but-unused code that misleads readers into thinking perceptual color
  is live.

## Consequences

- State color is an RGB lerp toward white/black — not perceptually uniform.
  Acceptable at the current ±15% deltas; large shifts on already-light/dark
  colors are slightly non-uniform.
- Re-introducing perceptual color means restoring an OKLCH implementation and
  rebaselining the golden harness, and warrants its own ADR.
