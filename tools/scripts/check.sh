#!/bin/bash

# Runs the C side of CI's Linux job in the same build directories: the style
# check, the default build with its export check and tests, the headless
# build, the Release build, and the codegen drift check. The Rust job (cargo
# build, test, clippy) is not run. Each step prints one line; a failing step
# prints its output and stops the script.
#
# Usage: check.sh [<base-ref>]   (passed to check_style.sh, default origin/main)

set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/../.."

log="$(mktemp)"
trap 'rm -f "$log"' EXIT

step() {
    local name="$1"
    shift
    if "$@" >"$log" 2>&1; then
        echo "ok    $name"
    else
        echo "FAIL  $name"
        cat "$log"
        exit 1
    fi
}

tools/scripts/check_style.sh "${1:-origin/main}"

step "configure Debug" cmake -S . -B build -G Ninja
step "build Debug" cmake --build build
step "exports" bash tools/scripts/check_exports.sh ./build/libflowi.so src/exports/flowi.version
step "loc_check" ./build/loc_check --manifest examples/data/localization/manifest.json --source examples
step "unit_tests Debug" ./build/unit_tests
if [ -x ./build/gpu_smoke_test ]; then
    step "gpu_smoke_test" env LIBGL_ALWAYS_SOFTWARE=1 ./build/gpu_smoke_test
else
    echo "skip  gpu_smoke_test (not built: EGL not found)"
fi

headless_is_sdl_free() {
    ! readelf -d ./build-headless/libflowi.so | command grep -q 'NEEDED.*libSDL2'
}

step "configure headless" cmake -S . -B build-headless -G Ninja -DFLOWI_SDL_BACKEND=OFF
step "build headless" cmake --build build-headless --target flowi unit_tests
step "headless library is SDL-free" headless_is_sdl_free
step "headless exports" bash tools/scripts/check_exports.sh ./build-headless/libflowi.so src/exports/flowi.version
step "unit_tests headless" ./build-headless/unit_tests

step "configure Release" cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
step "build Release" cmake --build build-release
step "unit_tests Release" ./build-release/unit_tests

step "codegen is current" cargo regen-check
