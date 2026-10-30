# The shipped shared library is windowed by default; headless is an opt-out cargo feature

Status: accepted (follows ADR-0058)

## Context

ADR-0058 made flowi build as a shared library (`libflowi.so`) with a curated, api_gen-generated
export surface, and switched `flowi_sys/build.rs` to configure that build with
`FLOWI_SDL_BACKEND=OFF`. That kept the shipped `libflowi.so` self-contained — no `DT_NEEDED`
on `libSDL2` — which was convenient for CI and for pure headless/offscreen consumers.

The cost was that the shared library shipped with *no* windowing backend: the SDL platform sources
(`flowi/src/platform/sdl/`, e.g. `backend_sdl_window.c`) were not compiled in. The Rust crate still exposes a windowed path
(`Application::new(w, h, /*headless=*/false)`, documented as "opens an SDL window"), but against
the shipped `libflowi.so` that path has no backend and panics at runtime. Windowing is the
common case for real consumers; headless is the exception (CI, golden/auto-testing). The default
was optimising for the exception.

A shared library's SDL dependency is all-or-nothing at the `.so` level (a `DT_NEEDED` entry),
so there is no per-consumer dead-stripping across the dynamic boundary the way static linking
gave. The choice is therefore which default the single artifact carries. CI already installs
`libsdl2-dev`, so SDL *availability* was never the real constraint — only whether a headless
consumer's `libflowi.so` carries an external runtime dependency it does not use.

## Decision

The default shipped `libflowi.so` is **windowed**: `flowi_sys` gains a `windowed` cargo feature,
enabled by default (and forwarded by the `flowi` crate as its own `windowed` feature). `build.rs` maps
the feature to `FLOWI_SDL_BACKEND` — `ON` when the feature is present, `OFF` when it is not — so
the default build compiles the SDL software host into `libflowi.so`.

Headless consumers opt out with `--no-default-features` (CI golden/unit-test builds do this), which
restores the SDL-free, self-contained `libflowi.so` of ADR-0058.

Consequences:

- The default `libflowi.so` gains `DT_NEEDED libSDL2`; every default consumer resolves `libSDL2.so`
  at load time, including a headless consumer that forgets `--no-default-features`. This is the
  deliberate cost of defaulting to the common case.
- The curated export surface is unchanged. SDL's own symbols stay hidden behind `local: *`; only
  the `fl_*` surface is exported and `scripts/check_exports.sh` must keep passing.
- Actually *opening* a window needs a display server, so it is not fully exercisable on headless CI
  without a virtual display. CI proves the windowed default *builds and links* against `libflowi.so`
  with SDL as `DT_NEEDED`; opening a real window is a manual/local (or Xvfb) check.
