#!/bin/sh
# opensmith_frontend_check.sh — run parser/AST roundtrip checks over corpus
#
# Usage:
#   opensmith_frontend_check.sh --tool <opensmithgen> --corpus-dir <dir>
#
# Runs opensmithgen --check-roundtrip and --ast on every .cst/.csp/.csmap
# fixture in the extracted corpus directory, reporting pass/fail counts.
set -e

die() { printf '%s\n' "$*" >&2; exit 1; }

TOOL=""
CORPUS_DIR=""

while [ $# -gt 0 ]; do
    case "$1" in
        --tool)       TOOL="$2"; shift 2 ;;
        --corpus-dir) CORPUS_DIR="$2"; shift 2 ;;
        *) die "unknown option: $1" ;;
    esac
done

[ -n "$TOOL" ]       || die "--tool required"
[ -n "$CORPUS_DIR" ] || die "--corpus-dir required"
[ -f "$TOOL" ]       || die "tool not found: $TOOL"
[ -d "$CORPUS_DIR" ] || die "corpus dir not found: $CORPUS_DIR"

TMPDIR_WORK=$(mktemp -d)
trap 'rm -rf "$TMPDIR_WORK"' EXIT

FAIL_ROUNDTRIP="$TMPDIR_WORK/fail_roundtrip"
FAIL_AST="$TMPDIR_WORK/fail_ast"
: > "$FAIL_ROUNDTRIP"
: > "$FAIL_AST"

find "$CORPUS_DIR" -type f \( -name '*.cst' -o -name '*.csp' -o -name '*.csmap' \) | sort \
    > "$TMPDIR_WORK/fixtures"

TOTAL=$(wc -l < "$TMPDIR_WORK/fixtures")
if [ "$TOTAL" -eq 0 ]; then
    printf 'no fixtures found in %s\n' "$CORPUS_DIR"
    exit 1
fi

while IFS= read -r fixture; do
    if ! "$TOOL" --check-roundtrip "$fixture" >/dev/null 2>&1; then
        printf '%s\n' "$fixture" >> "$FAIL_ROUNDTRIP"
        printf 'roundtrip FAIL: %s\n' "$fixture"
        continue
    fi
    if ! "$TOOL" --ast "$fixture" >/dev/null 2>&1; then
        printf '%s\n' "$fixture" >> "$FAIL_AST"
        printf 'ast FAIL: %s\n' "$fixture"
    fi
done < "$TMPDIR_WORK/fixtures"

ROUNDTRIP_FAIL=$(wc -l < "$FAIL_ROUNDTRIP")
AST_FAIL=$(wc -l < "$FAIL_AST")

printf 'fixtures checked: %d\n' "$TOTAL"
printf 'roundtrip failures: %d\n' "$ROUNDTRIP_FAIL"
printf 'ast failures: %d\n' "$AST_FAIL"

if [ "$ROUNDTRIP_FAIL" -ne 0 ] || [ "$AST_FAIL" -ne 0 ]; then
    exit 3
fi
