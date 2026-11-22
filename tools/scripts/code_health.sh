#!/bin/bash

# Prints the CodeScene Code Health score of flowi's own sources, one
# "score<TAB>path" line per file, lowest score first. Files CodeScene cannot
# score (most headers) are left out.
#
# Modes:
#   all (default)  every tracked first-party source file
#   changed        files that differ from origin/main, committed or not
#   <path>...      the given files; "origin/main:./src/x.c" scores a file as
#                  it is at that revision
#
# Needs the CodeScene CLI and CS_ACCESS_TOKEN. The CLI ships with the
# CodeScene MCP server; set CS_CLI to use another copy.

set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/../.."

find_cli() {
    if [ -n "${CS_CLI:-}" ]; then
        echo "$CS_CLI"
    elif command -v cs >/dev/null; then
        command -v cs
    else
        { ls -d "$HOME"/.cache/codehealth-mcp/MCP-*/cs 2>/dev/null || true; } | sort -V | tail -1
    fi
}

first_party() {
    command grep -E '\.(c|h|cpp|m|rs)$' \
        | command grep -E '^(src|tests|examples|rust|tools)/' \
        | command grep -v '/generated/'
}

CLI="$(find_cli)"
if [ -z "$CLI" ] || [ ! -x "$CLI" ]; then
    echo "code_health.sh: CodeScene CLI not found; set CS_CLI" >&2
    exit 2
fi
if [ -z "${CS_ACCESS_TOKEN:-}" ]; then
    echo "code_health.sh: CS_ACCESS_TOKEN is not set" >&2
    exit 2
fi

case "${1:-all}" in
    all)
        mapfile -t files < <(git ls-files | first_party)
        ;;
    changed)
        mapfile -t files < <(
            {
                git diff --name-only --diff-filter=ACMR origin/main
                git ls-files --others --exclude-standard
            } | sort -u | first_party
        )
        ;;
    *)
        files=("$@")
        ;;
esac

if [ ${#files[@]} -eq 0 ]; then
    exit 0
fi

printf '%s\n' "${files[@]}" \
    | CS_DISABLE_VERSION_CHECK=1 "$CLI" review --file-list - --output-format jsonl \
    | jq -r 'select(.score != null) | "\(.score)\t\(.path)"' \
    | sort -n
