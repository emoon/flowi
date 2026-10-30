# Font readiness for layout: no-poison measure cache + opt-in load barrier

Fonts load asynchronously (the file bytes are read off disk via the async VFS; the FreeType
face is built in the read callback). Until a font is **metrics-ready**, the Clay measure
function (`font_cache_get_text_size`) returns `(0,0)`. This breaks layout two ways, and the two
need different fixes.

First, the breakage is **permanent**, not transient. Clay caches each measurement keyed by
string contents + font + size (`Clay__MeasureTextCached`), and on a cache hit it returns the
stored value and only bumps the entry's generation — it never re-measures. Entries are evicted
only after going unseen for >2 generations, so a clip name that is on screen every frame is
never evicted: a `(0,0)` cached at frame 0 stays `(0,0)` forever, even after the font becomes
ready. So render-when-ready alone does **not** heal a visible string — the cache poisons.

Second, even with the cache fixed, the first frames would visibly **reflow** as fonts arrive
(`0` size → real size), which desyncs pixel comparisons and looks janky on startup. The first consumer
worked around both by hand-rolling a spin loop that ticked
empty frames until the font reported loaded before emitting any text — ugly, re-implemented an
existing primitive, and asserted on a tick cap that false-fails on a slow disk.

The decision is two coordinated changes:

- **The measure cache must never store a not-ready measurement.** In `Clay__MeasureTextCached`,
  after the per-word measure loop, if `text->length > 0 && measuredHeight == 0`, release the
  freshly-allocated cache item back to the freelist and return the DEFAULT item *before* it is
  linked into the bucket — so the string re-measures next frame and caches its real size once
  the font is ready. This is the correctness floor: it makes render-when-ready actually work,
  including for fonts loaded mid-session.

- **An opt-in `fl_font_ensure_loaded()` load barrier.** A no-arg call that pumps
  `font_cache_update()` while any font is still `Loading`, returning once every
  font is `Ready`/`Failed` (`false` if any failed). Setup code kicks off several font loads, then calls it once before
  the first rendered frame. It removes the startup reflow and makes pixel comparisons deterministic
  without per-test spins. It is **app policy** — the core's render-when-ready contract stays the
  default; the core never *requires* a wait.

## Considered Options

- **Where to break the poison: surgical no-cache vs reset-on-ready.** Clay exposes
  `Clay_ResetMeasureTextCache()`, so a Loading→Ready transition could just clear the whole
  measure cache. Rejected: it re-measures *all* text on every font-ready event, including text
  whose fonts were already loaded. Chose the surgical in-cache check — it touches only the
  strings that are actually not-ready, at the cost of the first deliberate edit into vendored
  `external/clay/clay.h` internals (a `flowi divergence` patch, as sanctioned by ADR-0012).

- **Readiness signal: width `0` vs height `0`.** Keyed the no-cache check on `measuredHeight`,
  not width. A non-empty string of only zero-advance codepoints (combining marks, U+200B) has
  width `0` but, once the font is ready, still reports a non-zero line height — so width would
  falsely treat it as not-ready and re-measure forever. Height is `0` only when metrics are
  absent; pairing it with `length > 0` keeps genuinely-empty strings cached.

- **Barrier scope: all pending fonts vs explicit set vs all resources.** Chose no-arg
  "all pending fonts." Fonts are the layout-poisoning resource; images self-heal (their
  dimensions are re-read each frame, never cached as a permanent fact), so blocking on them
  would needlessly stall startup and contradict "keep background loads streaming." An explicit
  list was rejected as verbose and forget-prone; draining all I/O was rejected because it would
  block the background loads the caller wants to keep running. The VFS has no global idle query,
  so a resource-agnostic barrier would not have been free anyway.

- **Make font loading fully synchronous.** Rejected: it contradicts the render-when-ready /
  no-blocking contract (CONTEXT.md *Resource Readiness*) and stalls the UI whenever a font is
  loaded mid-session.

- **Failure policy: loop-till-settled vs timeout vs assert.** Chose loop-till-settled with no
  timeout and no tick cap: a missing file resolves to `Failed` and exits the loop, so there is
  no hang to guard against except a genuinely-stuck VFS read (a bug to fix at the source, not
  paper over). The assert was rejected because the cache fix already makes a missing font
  non-fatal (it renders with fallback and reflows), so crashing a shipping app over a slow disk
  is the wrong trade.

## Consequences

- The barrier waits on **metrics-readiness** only; glyph rasterization (SW coverage bitmap, GPU
  atlas) stays render-when-ready and is never waited on. A backend that hasn't rasterized a
  glyph yet retries next frame.
- The consumer's font-activation helper shrinks to `load → ensure → set`; the spin, the empty-tick pumping,
  the tick cap, and the asserts are deleted.
- `fl_font_is_loaded` / `is_loaded()` stays as a cheap per-font poll for app-side conditional UI
  (render-when-ready spinners), but it is no longer the setup pattern, and hand-rolled spins on
  it are out.
- `fl_font_ensure_loaded()` is opt-in. A site that forgets it is not broken — text reflows once
  the font lands (the cache fix), only the first frames look unsettled.
- A genuinely-empty string (`length == 0`) re-measures only as cheaply as before (the per-word
  loop never runs); the no-cache path is gated on `length > 0` so it is never hit for empties.
