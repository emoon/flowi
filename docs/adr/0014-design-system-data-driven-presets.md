# Design system: data-driven theme presets; shadcn/Radix as reference, not foundation

## Status

accepted. Partly realised: flowi ships the semantic color table (`FlThemeColors`), sparse overrides
(`FlThemeOverride`, merged by `fl_theme_merge_override`), the 8px token scale (`design_tokens.def`) and a
hardcoded default palette (`ui_theme.h`). Loading a `theme.json` and a density setting are not implemented in
this repository; presets are app-supplied data.

flowi owns the design system as a shared, data-driven baseline — a semantic color
table (interactive / surface / border / text / feedback), an 8px spacing/radius/sizing
token scale, perceptual state derivation, and a neutral default preset — and each app
supplies its own `theme.json` + density on top. We treat shadcn's token taxonomy and
Radix's interaction semantics as a *reference to crib*, not a foundation: the
visual identity of a dense, dark audio-tool UI (Ableton Live, Vital) is the target,
because shadcn's airy, low-density SaaS aesthetic is wrong for it. The
same widget kit therefore wears many **theme presets** — a dense dark theme, a
shadcn-like preset for later projects — differing only in data, never in code.

## Considered alternatives

- **Adopt shadcn's aesthetic wholesale.** Rejected: its low-density, neutral-gray look
  reads as a SaaS dashboard, not a dense tool UI; and its component layer is React + Radix DOM,
  which does not map to flowi's immediate-mode C — only the look would transfer, and the
  look is the wrong one.
- **Per-app bespoke looks over a minimal framework.** Rejected: defeats the reuse that
  makes "modern look across applications" worth doing; the OKLCH
  derivation and token system are logic that belongs in the framework, not copy-pasted
  per app.

## Consequences

- The token system, OKLCH derivation, and a neutral default preset are framework
  deliverables in flowi; apps own only their `theme.json` + density overrides.
- shadcn becomes one theme preset among several, not the foundation. Radix informs the
  widget interaction work as a behavior reference only.
