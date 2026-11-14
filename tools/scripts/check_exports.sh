#!/usr/bin/env bash
# Checks that libflowi.so's dynamic symbol table is exactly the export list
# api_gen emitted. The version script (src/exports/flowi.version) names the
# public symbols and `nm -D` reports what the built library exports. The check
# fails on a symbol that leaked past `local: *` and on a listed symbol the
# library does not define.
#
# Usage: check_exports.sh [<libflowi.so>] [<flowi.version>]
# The defaults point into the repo-root build/ directory.
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

# Expected: the indented `  name;` lines of the version script's `global:`
# section. The `global:` and `local:` labels and the `*;` wildcard do not match.
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
