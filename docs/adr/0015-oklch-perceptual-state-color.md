# Adopt OKLCH perceptual color for state derivation

## Status

accepted — supersedes ADR-0004

## Decision

flowi derives interactive state colors (hover / press / focus) at runtime by
lightening/darkening a base color, and will derive variants from *arbitrary*
per-object user colors (track/clip colors). RGB and HSL lerps toward
white/black are not perceptually uniform and visibly misbehave on already-light or
saturated bases. We restore an OKLCH/OKLab module and do state and variant derivation
there. This supersedes ADR-0004, which deferred perceptual color while no shipping need
existed; the per-object-color and modern-look work is that need.

## Context

The modern token systems we treat as reference (Tailwind v4, current shadcn) moved to
OKLCH for exactly this reason. flowi's case is stronger than theirs: shadcn mostly
*enumerates* state tokens, whereas flowi *derives* them at runtime — derivation is
precisely where perceptual uniformity pays off.

## Consequences

- The OKLCH module removed in ADR-0004 is restored; state derivation is no longer a
  plain RGB lerp.
- Changes every interactive pixel, so it forced a rebaseline of the golden pixel
  harness (since removed from the test suite). Accepted as a deliberate, isolated visual change (not folded into a refactor).
- Runtime derivation (`fl_resolve_widget_colors`) reads `interactive.base` only. The
  default theme's pre-baked `interactive.hover`/`interactive.press` literals are now
  non-perceptual relative to the derived path, but they are largely unused at runtime
  (only written when constructing the default theme, never read for state derivation), so
  they are left unchanged. Host/app-side default-theme derivation is likewise out of scope.
