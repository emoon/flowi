# flowi's generated data-type layouts are a downstream `.so` plugin ABI

## Status

accepted

## Context

An embedding host's plugin SDK exposes flowi types directly to plugin authors (ADR-0042 — `Fl*`
for flowi-owned types, no aliasing). The host's plugins are **separately-compiled,
dynamically-loaded `.so`s** (loaded via `dlopen`/`LoadLibraryA`), including third-party
plugins shipped in other repositories. Those binaries are compiled against the flowi headers and bind to
the *memory layout* of the `Fl*` types — `FlString`, `FlWidgetConfig`, `FlTextConfig`, the vfs
structs, etc. (This coupling is the same whether the types are spelled `Fl*` directly or aliased;
ADR-0042 chose direct, so the coupling is visible rather than hidden.)

This is invisible from inside flowi. A flowi author reordering a field in `FlString`, widening an
enum, or changing a `:62`/`:1` bitfield split is a routine internal change — but it silently breaks
every already-built downstream plugin that linked the old layout, with no compile error anywhere in
the flowi tree.

## Decision

Treat the **layouts** of any IDL type that a downstream exposes across its plugin boundary as a
**stable ABI**, not an internal detail. Concretely:

- Changing the layout of an exposed type (field order, size, bitfield split, enum width) is
  an **ABI break** that requires a coordinated downstream rebuild of all plugins, not a silent
  internal edit.
- The set of layout-frozen types is exactly those a downstream exposes across its `.so` plugin
  boundary — flowi does not freeze its *whole* surface, only what a downstream actually puts in its
  plugin SDK headers.
- flowi remains free to change `fl_*` *function* signatures and any non-exported type: those are
  recompiled against, not dynamically bound.

flowi does not enforce this mechanically today; this ADR records the constraint so it is a known,
deliberate boundary rather than a surprise. A layout-assertion check (à la the compile-only
`tests/*_api_check.c` sources) over the exported subset is a reasonable future guard.

## Consequences

- flowi's "we own our types, change them freely" stance (ADR-0031/0033) has one carve-out: types a
  downstream puts in its plugin SDK are layout-frozen for that downstream's plugin ABI.
- The freeze is scoped — a type only becomes ABI-relevant when a downstream exposes it across its
  plugin boundary; flowi has no obligation for types no one ships to plugins.
- A future regression guard (compile-time layout assertions over the exposed set, owned by the
  downstream or contributed upstream) would turn the silent break into a build failure.
