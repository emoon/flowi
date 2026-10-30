#!/usr/bin/env bash
# Assert that libflowi.so's dynamic symbol table is EXACTLY the curated export list
# api_gen emitted. The version script (src/exports/flowi.version) names
# the public symbols (the foundation's, the VFS's and the hand-written headers'
# included); `nm -D` reports what the built shared library actually exports. Any drift — a symbol that leaked past `local: *`, or a curated symbol the
# library failed to define — fails the check.
#
# Usage: check_exports.sh [<libflowi.so>] [<flowi.version>]
# Defaults resolve the repo-root build/ layout so a bare `bash tools/scripts/check_exports.sh`
# works after `cmake --build build`.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
lib="${1:-${repo_root}/build/libflowi.so}"
version_script="${2:-${repo_root}/src/exports/flowi.version}"

if [[ ! -f "$lib" ]]; then
    echo "check_exports: shared library not found: $lib" >&2
    exit 1
fi
if [[ ! -f "$version_script" ]]; then
    echo "check_exports: version script not found: $version_script" >&2
    exit 1
fi

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

# Expected: the symbol names in the version script's `global:` section — the
# indented `  name;` lines (the `global:`/`local:` labels and the `*;` wildcard
# have no `;`-terminated identifier, so they drop out).
sed -n '/global:/,/local:/p' "$version_script" \
    | grep -E '^[[:space:]]+[A-Za-z_][A-Za-z0-9_]*;' \
    | sed -E 's/^[[:space:]]+//; s/;.*$//' \
    | sort -u > "$tmp/expected"

# Actual: defined (T/W/D/B/R) global symbols in the dynamic symbol table, with the
# `@@FLOWI_1.0` version tag the version script attaches stripped off.
nm -D --defined-only "$lib" \
    | awk 'NF >= 3 && $2 ~ /^[TWDBR]$/ { sub(/@@.*/, "", $3); print $3 }' \
    | sort -u > "$tmp/actual"

if diff -u "$tmp/expected" "$tmp/actual" > "$tmp/diff"; then
    echo "check_exports: OK — $(wc -l < "$tmp/expected") exported symbols match the curated list."
    exit 0
fi

echo "check_exports: FAIL — dynamic symbol table does not match the curated export list." >&2
echo "  (< expected from flowi.version, > actual from nm -D $lib)" >&2
cat "$tmp/diff" >&2
exit 1
