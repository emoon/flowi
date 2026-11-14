#!/bin/bash

# Runs clang-format over flowi's own C sources.
#
# Excluded:
#   - build dirs (build*/, cmake-build-*/) and the Rust target/ tree
#   - the whole external/ tree
#   - any */generated/* dir and the api_gen C output in include/flowi/. api_gen
#     emits both without a formatter pass and CI's `cargo regen-check` compares
#     them byte for byte. include/flowi/ has no /generated/ segment, so it has
#     its own exclusion.
#   - src/render/gpu/gpu_metal_internal.h, an Objective-C header. clang-format
#     detects it as ObjC, .clang-format only declares Language: Cpp, and the
#     config lookup fails with "Unsuitable", which aborts this script under
#     set -e. The backend's .m files are outside the glob below, so none of
#     the Metal sources are formatted.
#   - src/render/gpu/shaders/. The .glsl bodies and the per-backend compat
#     headers are spliced into shaders verbatim and are not compiled as C, and
#     clang-format would reflow the #defines in the headers.
#
# Modes: all (default) | staged | changed.

set -e

# Run from the repo root so the find and git paths below resolve.
cd "$(dirname "${BASH_SOURCE[0]}")/../.."

MODE="${1:-all}"

# Exclusion filter for the staged and changed modes.
filter_paths() {
    grep -E '\.(c|cpp|h|hpp)$' \
        | grep -v '/gpu_metal_internal\.h$' \
        | grep -v '/generated/' \
        | grep -v '^include/flowi/' \
        | grep -v '^src/render/gpu/shaders/' \
        | grep -v '^external/' \
        | grep -v '^build' \
        | grep -v '^rust/target/'
}

format_files() {
    local files=("$@")
    if [ ${#files[@]} -eq 0 ]; then
        echo "No files to format"
        return
    fi
    # Parallel: -P processes, -n files per process.
    printf '%s\n' "${files[@]}" | xargs -P 16 -n 8 clang-format --style=file:.clang-format -i
}

case "$MODE" in
    all)
        echo "Formatting all files..."
        # Prune the excluded directories so find does not descend into them.
        mapfile -t files < <(find . \
            -type d \( \
                   -path "./build*" \
                -o -path "./cmake-build-*" \
                -o -path "./rust/target" \
                -o -path "*/generated" \
                -o -path "./include/flowi" \
                -o -path "./src/render/gpu/shaders" \
                -o -path "./external" \
            \) -prune \
            -o -type f \( -name "*.c" -o -name "*.cpp" -o -name "*.h" -o -name "*.hpp" \) \
            -not -name "gpu_metal_internal.h" -print)
        format_files "${files[@]}"
        ;;

    staged)
        echo "Formatting staged files..."
        mapfile -t files < <(git diff --cached --name-only --diff-filter=ACM | filter_paths)
        format_files "${files[@]}"
        ;;

    changed)
        echo "Formatting changed files..."
        mapfile -t files < <(git diff --name-only --diff-filter=ACM HEAD | filter_paths)
        format_files "${files[@]}"
        ;;

    *)
        echo "Usage: $0 [all|staged|changed]"
        echo "  all     - Format all files (default)"
        echo "  staged  - Format only staged files"
        echo "  changed - Format only changed files (staged or unstaged)"
        exit 1
        ;;
esac

echo "Formatting complete"
