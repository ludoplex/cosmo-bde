#!/bin/sh
# opensmith_parity.sh — run deterministic parity harness over OpenSmith corpus
#
# Usage:
#   opensmith_parity.sh --inventory <lock.json> --corpus-dir <dir>
#                       --artifacts-dir <dir> [--engine <cmd>] [--dry-run]
#
# In dry-run mode (default when --engine is omitted), validates that all
# .cst/.csp/.csmap fixture files are present in the corpus directory.
# In engine mode, runs the given command on each fixture and captures output.
set -e

die() { printf '%s\n' "$*" >&2; exit 1; }

INVENTORY=""
CORPUS_DIR=""
ARTIFACTS_DIR=""
ENGINE=""
DRY_RUN=0

while [ $# -gt 0 ]; do
    case "$1" in
        --inventory)     INVENTORY="$2"; shift 2 ;;
        --corpus-dir)    CORPUS_DIR="$2"; shift 2 ;;
        --artifacts-dir) ARTIFACTS_DIR="$2"; shift 2 ;;
        --engine)        ENGINE="$2"; shift 2 ;;
        --dry-run)       DRY_RUN=1; shift ;;
        *) die "unknown option: $1" ;;
    esac
done

[ -n "$INVENTORY" ]     || die "--inventory required"
[ -n "$CORPUS_DIR" ]    || die "--corpus-dir required"
[ -n "$ARTIFACTS_DIR" ] || die "--artifacts-dir required"
[ -f "$INVENTORY" ]     || die "inventory not found: $INVENTORY"
[ -d "$CORPUS_DIR" ]    || die "corpus dir not found: $CORPUS_DIR"

# collect .cst/.csp/.csmap fixtures from extracted corpus
TMPDIR_WORK=$(mktemp -d)
trap 'rm -rf "$TMPDIR_WORK"' EXIT

find "$CORPUS_DIR" -type f \( -name '*.cst' -o -name '*.csp' -o -name '*.csmap' \) | sort \
    > "$TMPDIR_WORK/fixtures"

TOTAL=$(wc -l < "$TMPDIR_WORK/fixtures")

if [ "$TOTAL" -eq 0 ]; then
    printf 'no fixtures found in %s\n' "$CORPUS_DIR"
    exit 1
fi

if [ "$DRY_RUN" = "1" ] || [ -z "$ENGINE" ]; then
    printf 'dry-run ok: %d fixtures discovered\n' "$TOTAL"
    exit 0
fi

mkdir -p "$ARTIFACTS_DIR"

while IFS= read -r fixture; do
    REL="${fixture#$CORPUS_DIR/}"

    # build engine command: replace {input} token or append path
    case "$ENGINE" in
        *'{input}'*) CMD=$(printf '%s' "$ENGINE" | sed "s|{input}|$fixture|g") ;;
        *)           CMD="$ENGINE $fixture" ;;
    esac

    OUT_FILE="$ARTIFACTS_DIR/$REL.stdout"
    ERR_FILE="$ARTIFACTS_DIR/$REL.stderr"
    CODE_FILE="$ARTIFACTS_DIR/$REL.exitcode"

    mkdir -p "$(dirname "$OUT_FILE")"

    RC=0
    sh -c "$CMD" > "$OUT_FILE" 2> "$ERR_FILE" || RC=$?
    printf '%d\n' "$RC" > "$CODE_FILE"
done < "$TMPDIR_WORK/fixtures"

printf 'fixtures run: %d\n' "$TOTAL"
