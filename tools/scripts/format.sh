#!/bin/bash

# Run clang-format across flowi's owned C sources.
#
# Excluded:
#   - build dirs (build*/, cmake-build-*/) and the Rust target/ tree
#   - the whole external/ tree (clay, freetype2, nanosvg, utest and the
#     third-party deps fzy, mimalloc, stb, xxhash). flowi's own absorbed
#     foundation now lives in-repo under src and IS formatted
#     like the rest of flowi.
#   - any */generated/* dir AND the api_gen C output tree include/flowi/:
#     api_gen emits those byte-for-byte with no formatter post-pass, and
#     `cargo regen-check` is a CI gate, so reformatting them would break it. (The public C tree has no /generated/ segment, so
#     it needs its own exclusion.)
#   - src/render/gpu/gpu_metal_internal.h: an Objective-C header (it
#     #imports Metal and names id<MTLDevice>). clang-format detects it as ObjC,
#     .clang-format only declares Language: Cpp, and the config lookup then
#     fails with "Unsuitable" — which, under set -e, aborted this whole script
#     partway through. The backend's .m files are already out of the glob
#     below, so the Metal sources stay unformatted as a set.
#   - the GPU compute-shader source tree src/render/gpu/shaders/: it holds
#     embedded shader source — the .glsl bodies and the per-backend compat .h
#     vocabulary headers — which is spliced into shaders verbatim, not
#     compiled as C. clang-format would reflow the vocabulary #defines, so the
#     whole dir stays out (the .glsl files dodge the C glob already; the .h does
#     not).
#
# Modes: all (default) | staged | changed.

set -e

# Run from the repo root regardless of where the script is invoked from, so the
# find/git paths below resolve consistently (this script lives in tools/scripts/).
cd "$(dirname "${BASH_SOURCE[0]}")/../.."

MODE="${1:-all}"

# Shared exclusion filter for the git-driven modes (staged/changed).
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
        # Prune the excluded directories so find never DESCENDS into them — not
        # just filters their files out of the result. This matters for external/:
        # traversing + reformatting third-party trees churns foreign code and
        # breaks the generated-tree byte-identical CI gate.
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
