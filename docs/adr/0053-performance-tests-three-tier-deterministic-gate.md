# Performance tests gate on deterministic work, not wall-clock

## Status

accepted. Partly implemented. Tiers 1 and 3 exist as `perf_fence` cases (`scene_raster_fill`, `scene_text_elided`,
`scene_many_elements`, `scene_floating`, `scene_dyn_images`, `scene_widget_gallery` in `unit_tests`; `scene_zoom_change` in
`gpu_smoke_test`). Tier 2 (an instructions-retired assertion through `perf_counters`) is not implemented: it is not wired
into any scene. `FlFrameStats` also carries `scaled_image_setups`.

The counter plumbing is gated by `FLOWI_UI_INSPECTOR`, which the CMake build defines for every `flowi_objects`
configuration, so it is compiled into release builds as well (see ADR-0047).

## Context

flowi aims to stay fast even on slow machines (CPU/SW and GPU/SDF alike), but the repo has
**no performance tests, no timing gate, and no regression fence** of any kind. The raw
ingredients exist unused: `core/perf_scope.*` (a per-frame scope profiler with a 16.8 ms
budget, already wired into `ui_update_end`), `core/perf_counters.*` (Linux `perf_event_open`,
including `PERF_COUNTER_INSTRUCTIONS`), `get_monotonic_time_ns_os()`, and the headless
drivers both backends already use in tests (`headless_window_backend` for SW, EGL-surfaceless
llvmpipe for GPU).

The obvious approach — assert a frame lands under N milliseconds — does not survive contact
with CI. Wall-clock is non-deterministic (thermal, scheduler, noisy neighbours), so a ns
threshold is either flaky or set so loose it catches nothing. Worse, the CI environment is
hostile to the two things that would help: GitHub-hosted runners are VMs where the GPU is
llvmpipe (GPU timings are meaningless) and `perf_event_open` is disabled (hardware counters
read back as zero). So the naive "is it fast?" gate cannot run where merges are gated.

The exactly-two-test-executables rule (`unit_tests`, `gpu_smoke_test`) means any perf test is
a new *case* inside one of those, not a new binary.

The insight that unblocks this: **wall-clock is not the only real-time signal, and it is the
worst one to assert on.** The *amount of work* a frame does is deterministic, and
instructions-retired is a deterministic proxy for time (reproducible run-to-run to well under
1% on a fixed binary + fixed input, immune to thermal/scheduler noise). So we gate on work,
observe time.

## Decision

Performance is fenced by **three tiers, ranked by determinism**, measured against a fixed set
of **isolating micro-scenes** (`scene_raster_fill`, `scene_text_elided`, `scene_many_elements`,
`scene_floating`, `scene_dyn_images`, `scene_zoom_change`, each parameterized by element count
N; plus `scene_widget_gallery` as a realistic composite). Each scene isolates one known hot
path so a regression points at a cause.

- **Tier 1 — deterministic work counters (the CI gate).** A small `FlFrameStats` struct each
  backend fills during `render()`, read back after a frame, carrying only numbers that map to a
  known hot path: `render_commands` (reuse the existing `ui_get_render_command_count()`),
  SW `bin_alloc_bytes` / `clip_broadcasts`, GPU `glyph_dispatches` / `svg_dispatches` /
  `tile_overflow`. These are machine-independent and flake-free, so they gate every PR in CI.
  A change fails the build when it makes the *work* bigger, not when a clock is slow.
- **Tier 2 — instructions-retired (the real-time proxy gate, local).** Via `perf_counters`
  (`PERF_COUNTER_INSTRUCTIONS`). Deterministic enough to assert on, and it catches
  constant-factor regressions tier 1 is blind to (a lost SIMD path, a new scalar loop that
  emits the same command count). It **asserts when `perf_counters_available()`** (bare-metal
  Linux dev box / self-hosted runner) and **auto-skips otherwise**, including GitHub CI VMs.
- **Tier 3 — wall-clock ns (observed only).** `get_monotonic_time_ns_os()` around each scene.
  Printed for human sanity ("do the proxies track real time on real hardware?"), **never
  asserted** — it is not deterministic and must never gate a build.

`FlFrameStats` and the scenes are gated behind the existing `FLOWI_UI_INSPECTOR`
seam (ADR-0047). The intent was that release builds carry no counter plumbing; as built, the seam is defined
unconditionally, so the plumbing is always compiled in. CPU scenes live as `UTEST`
cases in `unit_tests` (runs in CI); GPU scenes live in `gpu_smoke_test` (EGL-gated; it runs in CI on llvmpipe,
where only the deterministic counters are meaningful).

**Baselines are locked to today's cost.** The first landing establishes the fence at current
numbers; it does not optimize. Optimizations follow separately, each one gated by a scene whose counter must
*drop* to prove the win.

## Considered alternatives

- **Assert wall-clock ms against a reference machine.** Rejected as the gate: non-deterministic,
  flaky in CI, and meaningless on the llvmpipe GPU runner. Kept as tier 3, observation-only.
- **Stand up a self-hosted bare-metal perf runner so tier 2 gates every PR.** Rejected for now:
  real ops burden and a machine to maintain forever, disproportionate to the payoff. The
  skip-if-unavailable design leaves this open as a later upgrade with no test changes.
- **Compute all counters externally from the public IR, no engine plumbing.** Rejected: command
  count is derivable, but glyph dispatches, real clip broadcast, and tile overflow are
  backend-internal — the highest-value GPU/SW regressions would be invisible.
- **Always-on `FlFrameStats` in release builds.** Rejected as a design goal: adds counter increments to release
  hot paths for a test-only need. (The build as shipped defines the gate unconditionally, so this alternative is
  de facto the current state.)
- **Realistic whole-app scenes only (a multi-panel editor).** Rejected as the primary set: a
  regression says "the editor got slower" without a cause, and no such application is built in this repository,
  so such scenes are partly speculative. One composite (`widget_gallery`) is kept as a smoke.

## Consequences

- CI gains a flake-free perf fence (tier 1) with no new test executable and no GPU/PMU
  dependency; the GPU path and instruction budgets are enforced wherever the hardware allows
  (tier 2), and skipped, not failed, where it does not.
- The fence is a *ratchet*: baselines start at today's cost and each optimization lowers a
  counter, so wins are proven by a number dropping rather than asserted in prose.
- Adding a hot path later means adding its scene + its `FlFrameStats` counter + a today's-cost
  budget — the pattern is fixed, so this stays mechanical.
- Tier 2 can be promoted to a PR gate later by adding a self-hosted runner, with no change to
  the tests themselves.
