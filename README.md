# flowi

[![CI](https://github.com/emoon/flowi/actions/workflows/ci.yml/badge.svg)](https://github.com/emoon/flowi/actions/workflows/ci.yml)

An immediate-mode UI toolkit written in C, with generated Rust bindings.

> Early work in progress. flowi is young and changing quickly, so expect missing features, rough edges
> and breaking API changes. See [Status](#status).

You declare the whole UI every frame, with no retained widget tree and no callbacks to keep in sync.
Layout is computed by Clay, the UI core emits a backend-agnostic render command list, and a
pluggable render backend turns that into pixels. The toolkit includes docking, menus, lists,
theming and localization.

This project uses a mix of hand-written and AI-generated code.

## Status

flowi is pre-1.0. There are no releases and no API stability promise: headers, the C ABI and the
Rust crate change without deprecation cycles. It builds and runs on Linux, macOS and Windows (see
[Platforms](#platforms)). Build it from source and track the repository instead of pinning a version.

## What it looks like

C, through the `fl_app` runner:

```c
#include <flowi/app/app.h>
#include <flowi/core/string.h>
#include <flowi/ui/layout_area.h>
#include <flowi/ui/value_helpers.h>

static void frame(void* user_data) {
    (void)user_data;
    fl_layout_area(.sizing = { .width = fl_sizing_grow(), .height = fl_sizing_grow() },
                   .color = fl_color(24, 24, 32, 255),
                   .child_alignment = { FlLayoutAlignmentX_Center, FlLayoutAlignmentY_Center }) {
        fl_layout_area(.sizing = { .width = fl_sizing_fixed(240), .height = fl_sizing_fixed(160) },
                       .color = fl_color(200, 40, 50, 255)) {}
    }
}

int main(void) {
    FlAppSettings settings = { .window_title = FL_S("hello"), .width = 1280, .height = 720 };
    FlApp* app = fl_app_create(&settings);
    fl_app_run(app, NULL, frame); // blocks, calling frame() until the window closes
    fl_app_destroy(app);
}
```

That is the `fl_*` C ABI the shared library exports, from the public headers in
`include/flowi`. `fl_app_create` brings the process-wide core services up itself; a host that
wants a specific job-thread count, or the main arena before there is an application, calls
`fl_init(job_threads)` first. The programs in `examples/` are built inside the tree and call the
same toolkit through its internal `ui_*` headers instead.

Rust, over the same core:

```rust
use flowi::{col, grow, row, Application, Button};

fn main() {
    let app = Application::new(800, 600, false);
    app.run(0u64, |clicks: &mut u64| {
        col! { width: grow!(), height: grow!() {
            row! { {
                if Button::new("Click me", Default::default()).clicked {
                    *clicks += 1;
                }
            } }
        }}
    });
}
```

## Building

Requirements: CMake 3.16+, Ninja (or Make), a C11 compiler (clang or gcc; ClangCL on Windows)
plus a C++ compiler (the ABI probe in `tests/probes/` is C++), zlib, and the SDL2 development
package. On Linux the GPU backend also needs the EGL and OpenGL development packages
(Debian/Ubuntu: `cmake ninja-build clang libsdl2-dev zlib1g-dev libegl1-mesa-dev libgl1-mesa-dev`).
The windowed build links SDL2; deploy the matching SDL2 runtime with your application.

```sh
cmake -S . -B build -G Ninja
cmake --build build
```

For an offscreen or embedded consumer that never opens a native window, opt out of the window host:

```sh
cmake -S . -B build-headless -G Ninja -DFLOWI_SDL_BACKEND=OFF
cmake --build build-headless --target flowi
```

The headless artifact has no SDL2 runtime dependency. The test executables (`unit_tests`,
`gpu_smoke_test`) use the headless platform backend either way, so disabling the window host does
not disable them.

The examples are built with the library whenever the SDL backend is on (`-DFLOWI_BUILD_EXAMPLES=OFF`
skips them). A Release build is `-DCMAKE_BUILD_TYPE=Release`.

### Rust

The `flowi` crate enables its `windowed` feature by default, so a normal build produces the same
windowed shared library:

```sh
cargo build --manifest-path rust/Cargo.toml -p flowi
cargo build --manifest-path rust/Cargo.toml -p flowi --no-default-features   # SDL-free
```

`--no-default-features` drops `windowed`, so the opt-out applies through the whole native build.

## Running an example

`examples/` holds six programs. The two showcases are `widget_gallery` (buttons, checkboxes,
switches, sliders, dropdowns, tab bars, lists, menus, tooltips, message boxes) and `dock_demo`
(an authored dock layout with drag-to-dock, tab groups, hide/show/close).

Run them from the repo root, because the runner mounts the working directory as its asset mount and
the examples load their font from a plain relative path:

```sh
cmake --build build --target widget_gallery
./build/examples/widget_gallery/widget_gallery            # GPU backend (default)
./build/examples/widget_gallery/widget_gallery --software # SDL software renderer
```

```sh
cmake --build build --target dock_demo
./build/examples/dock_demo/dock_demo
```

Rust examples run the usual way:

```sh
cargo run --manifest-path rust/Cargo.toml -p flowi --example button
```

The other examples are `app_smoke` (smallest possible `fl_app` program), `two_windows`,
and `shared_smoke` (the shared library's C ABI).

## Backends

- The GPU backend is all-compute: coverage is computed from glyph and shape outlines instead of
  being rasterized by fixed-function hardware. OpenGL 4.3 core is the floor on Linux and Windows;
  macOS uses Metal, since Apple caps OpenGL at 4.1. Compute shaders are single-sourced and compiled
  per backend.
- The software backend is a CPU rasterizer, always available and used for the headless test suite.

The backend is a value the host constructs and injects; both can live in one binary and are chosen
at runtime.

## Platforms

| Platform | Windowed build | GPU backend | CI |
| --- | --- | --- | --- |
| Linux | SDL2 | OpenGL 4.3 compute | full build + `unit_tests` + `gpu_smoke_test` on llvmpipe |
| macOS | SDL2 | Metal | full build + `unit_tests` + `gpu_smoke_test` on the runner's MTLDevice |
| Windows | SDL2 | OpenGL 4.3 compute | headless shared library + `unit_tests` |

The windowed and GPU paths on Windows are built and run by hand, not by CI.

CI also builds and tests the Rust crate and checks that the generated bindings are up to date.

## Documentation

- [`CONTEXT.md`](docs/CONTEXT.md): the domain vocabulary: what a Render Backend, Render Command,
  Workspace, Detachable Panel and so on mean here. Read this before the code.
- [`docs/adr/`](docs/adr): the architecture decisions and why they went the way they did.
- [`CONTRIBUTING.md`](docs/CONTRIBUTING.md): how to build, where a new test goes, the comment
  and codegen rules, what CI gates, and the licensing terms.

## License

MIT, see [LICENSE](LICENSE). `external/` holds third-party code under its own licenses; see [THIRD_PARTY_LICENSES.md](docs/THIRD_PARTY_LICENSES.md).
