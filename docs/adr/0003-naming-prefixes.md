# Naming prefixes: `Em` for the core, `EmUi` for the UI API

## Status

superseded by ADR-0031 — the `Em`/`EmUi` two-tier split is dropped in favor of a single `Fl`/`fl_`
prefix across the whole library (the pre-release name is rebranded to flowi and `base` is absorbed). The
planned `base`→`Em*` rename sweep below became a `base`→`Fl*` sweep, which has landed (`FlVec2`,
`FlColor`, `FlRect`, ...). The *reasoning* here (why a consumer's prefix is wrong for the owner, why
unprefixed `base` names are collision-prone) still holds; only the chosen letters changed.

## Context

The library carried several naming regimes at once. The `base` foundation types were
**unprefixed** (`Vec2`, arena types, scalar `u8`/`f32`), and the UI core used them raw
(`font_cache_get_text_size(...) -> Vec2`). The public plugin ABI used a **legacy consumer prefix** — 258
symbols in a legacy plugin-ABI header — inherited from the host application the library was
originally built for. The UI core itself had **no prefix of its own**: it borrowed `base`
types and the consumer-prefixed types directly.

Introducing the neutral render-command IR and `RenderBackend` (ADR-0001) means minting a
batch of brand-new types that belong to **the library itself** — not to `base`, and not to
that host's ABI. They need a home prefix. Choosing the consumer's prefix would misrepresent ownership and
an `Ir`-style prefix would name a *module*, not an *owner*.
Separately, unprefixed foundation names like `Vec2` are collision-prone against vendored
code (Clay, etc.).

## Decision

A **two-tier prefix**, modeled on Dear ImGui's `Im` / `ImGui` split:

- **`Em`** — the library's low-level / shared / render-layer types: `EmColor`, `EmRect`,
  `EmEffect`, `EmRenderCmd`, `EmRenderBackend`, etc. (cf. ImGui's `ImVec2`, `ImDrawCmd`).
- **`EmUi`** — the library's high-level UI/widget API surface — the eventual successor to today's
  public consumer-prefixed types (cf. ImGui's `ImGuiContext`, `ImGuiIO`).

Supporting rules:

- **`base` stays the unprefixed foundation for now** and is reused as-is. The IR reuses
  `Vec2` and introduces `EmColor` / `EmRect` (no internal `Color`/`Rect` existed).
- **The consumer-prefixed set remains the frozen public host ABI**; the IR/backend work does not touch it.
- New code from the IR/backend work onward uses `Em`. Reviewers reject new
  consumer-prefixed/unprefixed core types.

## Considered alternatives

- **Keep the consumer prefix for new internal types too.** Rejected: it is a consumer's (the host
  application's) prefix; baking it into the library's own core misrepresents ownership and blocks reuse by
  other consumers.
- **A single flat `Em` for everything.** Rejected: loses the low-level/API distinction
  ImGui found valuable; `EmUi` reserves a clean namespace for the high-level API successor
  to the consumer-prefixed types.
- **An `Ir`-style module prefix for the IR types.** Rejected: names a module, not an owner;
  the IR is library-core like the rest.
- **Mass-rename the consumer-prefixed types and `base` (`Vec2` → `EmVec2`) now.** Deferred: large,
  mechanical, and orthogonal to freezing the IR.

## Consequences

- The codebase is **transiently mixed**: new `Em*` types sit alongside `base`'s `Vec2` and
  the consumer-prefixed ABI. Accepted as a migration state.
- A later, dedicated **rename sweep** moves `base` foundation types to `Em*` (`Vec2` →
  `EmVec2`, shedding collision-prone unprefixed names) and migrates the public ABI from
  the consumer prefix to `EmUi*`. Until then, IR-era code reuses `Vec2` and is renamed along with
  everything else in that sweep.
- New types get the `Em` prefix from the IR work onward.
