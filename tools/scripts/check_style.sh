#!/bin/bash

# Checks flowi's own sources against the style rules a compiler does not
# enforce.
#
# Errors, checked over the whole tree, fail the script:
#   - a full-width run of slashes used as a separator line
#   - NULL in src/ or tests/ (flowi uses nullptr)
#   - sizeof(a) / sizeof(a[0]) (flowi uses sizeof_array)
#   - a null check on g_ui_ctx
#   - a Claude session link or trailer in a commit message
#
# Notices, checked over the lines a change adds or removes, are printed for
# the reviewer and do not fail the script:
#   - an added comment line in C code
#   - an added counting loop written without for_count
#   - a line removed from the public API (api/*.def, include/flowi/)
#
# Usage: check_style.sh [staged | <base-ref>]
#   staged      the change is what is staged for commit
#   <base-ref>  the change is everything since the branch left <base-ref>
#               (default origin/main)
#
# Not checked: external/. The NULL rule also skips tests/probes/, examples/
# and Objective-C files.

set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/../.."

C_FILES=('*.c' '*.h' '*.cpp')

errors=0

# Prints the matches of a banned pattern under a heading and counts them.
ban() {
    local heading="$1" pattern="$2"
    shift 2
    local hits
    hits="$(git grep -nE "$pattern" -- "$@" || true)"
    if [ -n "$hits" ]; then
        echo "error: $heading" >&2
        echo "$hits" | sed 's/^/  /' >&2
        errors=$((errors + 1))
    fi
}

ban "full-width separator line" '^[[:space:]]*/{20,}[[:space:]]*$' \
    src tests examples tools include
ban "NULL (use nullptr)" '\bNULL\b' \
    "${C_FILES[@]/#/src/}" "${C_FILES[@]/#/tests/}" ':!tests/probes'
ban "open-coded array length (use sizeof_array)" \
    'sizeof\(([^()]+)\) */ *sizeof\((\(?\1\)?\[0\]|\*\1)\)' \
    src tests examples
ban "null check on g_ui_ctx" \
    'g_ui_ctx *(==|!=) *nullptr|nullptr *(==|!=) *g_ui_ctx|(if|while|assert|FL_ASSERT) *\(!?g_ui_ctx *(\)|&&|\|\|)|!g_ui_ctx *(\)|&&|\|\|)|g_ui_ctx *\?[^:]|FL_VALIDATE[A-Z_]*\(g_ui_ctx *[,)]' \
    src

if [ "${1:-}" = "staged" ]; then
    diff_args=(--cached)
    log_range=""
else
    base="${1:-origin/main}"
    if git rev-parse --verify --quiet "$base" >/dev/null; then
        diff_args=(--merge-base "$base")
        log_range="$base..HEAD"
    else
        echo "check_style: $base not found, skipping the change checks" >&2
        diff_args=()
        log_range=""
    fi
fi

if [ -n "$log_range" ]; then
    hits="$(git log --format='%h %s%n%b' "$log_range" \
        | command grep -nE '^Claude-Session:|https://claude\.ai/code/session' || true)"
    if [ -n "$hits" ]; then
        echo "error: Claude session link in a commit message" >&2
        echo "$hits" | sed 's/^/  /' >&2
        errors=$((errors + 1))
    fi
fi

# Prints the diff lines that match a pattern, each prefixed with its file.
changed_lines() {
    local pattern="$1"
    shift
    git diff "${diff_args[@]}" --unified=0 --no-color -- "$@" \
        | awk -v pattern="$pattern" '
            /^\+\+\+ b\// { file = substr($0, 7); next }
            /^--- a\//    { if (file == "") file = substr($0, 7); next }
            /^diff /      { file = ""; next }
            $0 ~ pattern  { print "  " file ": " substr($0, 2) }'
}

notice() {
    local heading="$1" lines="$2"
    if [ -n "$lines" ]; then
        echo "notice: $heading"
        echo "$lines"
    fi
}

if [ ${#diff_args[@]} -gt 0 ]; then
    notice "added comment lines (comments say what the code does; no rationale, ADR-0044)" \
        "$(changed_lines '^\\+[[:space:]]*(//|/\\*)' \
            "${C_FILES[@]/#/src/}" "${C_FILES[@]/#/tests/}" "${C_FILES[@]/#/examples/}")"
    notice "added counting loops (use for_count)" \
        "$(changed_lines '^\\+.*for \\([a-z0-9_]+ [a-z_]+ = 0; [a-z_]+ < [^;]+; *(\\+\\+[a-z_]+|[a-z_]+\\+\\+)\\)' \
            src tests examples)"
    notice "lines removed from the public API (name the consumers checked)" \
        "$(changed_lines '^-[^-]' 'api/*.def' include/flowi)"
fi

if [ "$errors" -gt 0 ]; then
    exit 1
fi
