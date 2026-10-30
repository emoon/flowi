# Contributing

flowi is pre-1.0 and moving fast; there is no API stability promise. Bug reports,
small fixes and focused pull requests are all welcome.

Read [`CONTEXT.md`](CONTEXT.md) before the code — it defines the vocabulary
(Render Backend, Render Command, Workspace, Detachable Panel, …). The decisions
behind the architecture live in [`docs/adr/`](adr).

## Building

Requirements: CMake 3.16+, Ninja (or Make), a C11 compiler (clang or gcc; ClangCL
on Windows) plus a C++ compiler (the ABI probe in `tests/probes/` is C++), zlib, the
SDL2 development package, and on Linux the EGL and OpenGL development packages. On
Debian/Ubuntu:

```sh
sudo apt-get install cmake ninja-build clang libsdl2-dev zlib1g-dev \
    libegl1-mesa-dev libgl1-mesa-dev
```

```sh
cmake -S . -B build -G Ninja
cmake --build build
```

Use one build directory and reuse it. The Rust crates build over the same C
library:

```sh
cargo build --manifest-path rust/Cargo.toml
```

See the [README](README.md#building) for the headless (`-DFLOWI_SDL_BACKEND=OFF`)
build, the Release build and for running the examples.

## Running the tests

```sh
./build/unit_tests                                  # all CPU tests
LIBGL_ALWAYS_SOFTWARE=1 ./build/gpu_smoke_test      # all GPU tests
./build/unit_tests --filter='dropdown.*'            # one group; a single glob, not a list
cargo test --manifest-path rust/Cargo.toml
```

## Tests: exactly two executables

There are two test executables and **no third may be added**:

- **`unit_tests`** — every CPU test, as a [utest.h](../external/utest) `UTEST` case.
  Pure unit tests and headless render tests alike.
- **`gpu_smoke_test`** — every GPU test. Configured only when EGL (Linux) or
  Metal (macOS) is found.

A new test is a new `UTEST` case in an existing file, never a new executable and
rarely a new file. To find the file a group lives in:

```sh
grep -rl 'UTEST(dropdown,' tests/
```

Cases live one file per area (`tests/unit_tests_<area>.c`); the rule below is the one that
will bite you when adding a case.

### No hardware-dependent assertions

Render tests assert through probes computed from the configured geometry, never
by comparing whole framebuffers. A whole-buffer hash only reproduces on the
machine that authored it (libm dispatches on CPU features, FMA contraction
varies), so it fails by *which host drew it* rather than by what the code did.
The same rule rules out float-exact comparisons, sleep-timed assertions, and
assertions on pointer identity.

## Generated code is off-limits

Anything under a `*/generated/*` directory, plus the public C headers in
`include/flowi/` and the export lists in `src/exports/`, is written by
`api_gen` from the IDL in `api/*.def`. Edit the `.def` and
regenerate:

```sh
cargo regen
```

Regeneration is deterministic, and `cargo regen-check` asserts the committed
output still matches the IDL, so a hand-edited generated file fails the build.
The generator lives in [emoon/api-gen](https://github.com/emoon/api-gen);
`tools/codegen/Cargo.toml` pins the exact commit that produced what is committed here.
To bump the generator, change `rev` there, run `cargo regen`, and commit the
updated `tools/codegen/Cargo.lock` with the regenerated output.

Public doc comments (`///` in a `.def`) are caller-facing reference only.
api_gen rejects issue refs, `ADR` refs and `(*.def)`/`(*.h)` source pointers
outright (ADR-0044).

## Comments

- **Comments say what the code does.** Rationale, design notes and "why it is
  built this way" belong in an ADR under `docs/adr/`, not in a comment
  (ADR-0044).
- **No issue numbers in code comments.** A comment describes the code, not the
  ticket that prompted it.
- New C functions are preceded by the full-width `///////…` separator line used
  throughout the sources.

## Naming

- Public C ABI: `fl_*` functions, `Fl*` types (`fl_menu_radio`, `FlColor`).
- Internal code: no `fl_` prefix — `ui_*` in `src/ui/`, plain names in
  `src/core/`.
- Rust bindings drop the prefixes; `fl_menu_radio_impl` is `Menu::radio`
  (ADR-0027).

Details and the config-struct call shape are in ADR-0045.

## Formatting

The pre-commit hook is **opt-in per clone** — enable it once:

```sh
git config core.hooksPath .githooks
```

It runs clang-format on staged C sources and rustfmt on staged `.rs` files,
skipping the generated trees. Without the hook, run `tools/scripts/format.sh staged`
yourself. Never run a package-level `cargo fmt`: rustfmt follows `mod`
declarations into the generated trees and churns them. Format per file.

## What CI gates

`.github/workflows/ci.yml` must be green:

- **Linux + macOS** — full C build, `unit_tests`, and `gpu_smoke_test` (on
  llvmpipe via surfaceless EGL on Linux, on the runner's `MTLDevice` on macOS),
  plus a headless (`-DFLOWI_SDL_BACKEND=OFF`) build and the curated-export
  check.
- **Windows** — headless shared library and `unit_tests` through `flowi.lib`.
  The windowed and GPU paths on Windows are built and run by hand, not by CI.
- **Rust** — build, tests, `clippy -D warnings`, and `cargo regen-check`, which
  asserts the committed codegen matches the IDL.

"Green on CI" means exactly these jobs. Any GPU claim beyond llvmpipe (hardware
drivers, other platforms) is local-only and must be worded as such.

**llvmpipe floor:** Mesa 26.0.6 + LLVM 22.1.3 aborts with `LLVM ERROR: Cannot
select ... MGATHER` on any compute-shader SSBO read. Mesa 26.1.1 + LLVM 22.1.5
works. If you run the GPU tests locally on llvmpipe, you need at or above that
pair; older LTS-distro Mesa and hardware drivers are unaffected.

## Pull requests

Keep a PR to one change. Say what it does and how you verified it, and note
anything you could not test (a platform you do not have). If the change is an
architectural decision rather than an implementation of one, add an ADR in
`docs/adr/` in the same PR.

## License

flowi is MIT licensed; see [LICENSE](../LICENSE). By contributing you agree your
contribution is licensed under the same terms.

flowi-authored source files do not carry per-file copyright or SPDX headers —
the root `LICENSE` covers them. Do not add them. Third-party code copied into
`src` (listed in [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md))
keeps its upstream copyright and license notice in the file; never remove or
edit those notices.

`external/` holds third-party code under its own licenses (zlib, MIT, BSD-2,
public domain, and FreeType's FTL); those files keep whatever headers upstream
ships. `*/generated/*` is owned by api_gen and is rewritten on every run.
