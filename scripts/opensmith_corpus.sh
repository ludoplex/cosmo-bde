#!/bin/sh
# opensmith_corpus.sh — build/extract deterministic OpenSmith fixture corpus
#
# Usage:
#   opensmith_corpus.sh inventory --zip <path> --lock <path>
#   opensmith_corpus.sh extract   --zip <path> --lock <path> --out-dir <dir>
#
# Requires: unzip, sha256sum (Linux) or shasum (macOS)
set -e

die() { printf '%s\n' "$*" >&2; exit 1; }

sha256_of_bytes() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum | cut -d' ' -f1
    elif command -v shasum >/dev/null 2>&1; then
        shasum -a 256 | cut -d' ' -f1
    else
        die "no sha256 tool found (need sha256sum or shasum)"
    fi
}

sha256_of_file() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | cut -d' ' -f1
    elif command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "$1" | cut -d' ' -f1
    else
        die "no sha256 tool found (need sha256sum or shasum)"
    fi
}

json_escape() {
    # minimal JSON string escaping: backslash and double-quote
    printf '%s' "$1" | sed 's/\\/\\\\/g; s/"/\\"/g'
}

# ──────────────────────────────────────────────────────────────────────────────
# inventory: scan zip and write deterministic lock JSON
# ──────────────────────────────────────────────────────────────────────────────
cmd_inventory() {
    ZIP=""
    LOCK=""
    NESTED_PREFIX="Samples"

    while [ $# -gt 0 ]; do
        case "$1" in
            --zip)            ZIP="$2"; shift 2 ;;
            --lock)           LOCK="$2"; shift 2 ;;
            --nested-zip-prefix) NESTED_PREFIX="$2"; shift 2 ;;
            *) die "unknown option: $1" ;;
        esac
    done

    [ -n "$ZIP" ]  || die "--zip required"
    [ -n "$LOCK" ] || die "--lock required"
    [ -f "$ZIP" ]  || die "zip not found: $ZIP"

    TMPDIR_WORK=$(mktemp -d)
    trap 'rm -rf "$TMPDIR_WORK"' EXIT

    unzip -q "$ZIP" -d "$TMPDIR_WORK/outer"

    ZIP_NAME=$(basename "$ZIP")
    ZIP_SHA=$(sha256_of_file "$ZIP")

    # collect entries: outer then nested
    ENTRIES_FILE="$TMPDIR_WORK/entries.json"
    : > "$ENTRIES_FILE"

    TOTAL=0
    find "$TMPDIR_WORK/outer" -type f | sort | while IFS= read -r f; do
        REL="${f#$TMPDIR_WORK/outer/}"
        EXT=$(printf '%s' "$REL" | sed 's/.*\.//' | tr '[:upper:]' '[:lower:]')
        case ".$EXT" in
            .cst|.csp|.csmap|.xsd|.xml|.json) ;;
            *) continue ;;
        esac
        SIZE=$(wc -c < "$f")
        SHA=$(sha256_of_file "$f")
        printf '{"container":"outer","entry":"%s","ext":".%s","sha256":"%s","size":%d}\n' \
            "$(json_escape "$REL")" "$EXT" "$SHA" "$SIZE" >> "$ENTRIES_FILE"
    done

    # nested zips under $NESTED_PREFIX/
    find "$TMPDIR_WORK/outer/$NESTED_PREFIX" -name '*.zip' -type f 2>/dev/null | sort | while IFS= read -r nzip; do
        NREL="${nzip#$TMPDIR_WORK/outer/}"
        NTMP="$TMPDIR_WORK/nested_$(basename "$nzip" .zip)"
        mkdir -p "$NTMP"
        unzip -q "$nzip" -d "$NTMP" 2>/dev/null || continue
        find "$NTMP" -type f | sort | while IFS= read -r f; do
            REL="${f#$NTMP/}"
            EXT=$(printf '%s' "$REL" | sed 's/.*\.//' | tr '[:upper:]' '[:lower:]')
            case ".$EXT" in
                .cst|.csp|.csmap|.xsd|.xml|.json) ;;
                *) continue ;;
            esac
            SIZE=$(wc -c < "$f")
            SHA=$(sha256_of_file "$f")
            printf '{"container":"%s","entry":"%s","ext":".%s","sha256":"%s","size":%d}\n' \
                "$(json_escape "$NREL")" "$(json_escape "$REL")" "$EXT" "$SHA" "$SIZE" >> "$ENTRIES_FILE"
        done
    done

    TOTAL=$(wc -l < "$ENTRIES_FILE")

    LOCK_DIR=$(dirname "$LOCK")
    mkdir -p "$LOCK_DIR"

    {
        printf '{\n'
        printf '  "extensions": [".cst",".csp",".csmap",".json",".xml",".xsd"],\n'
        printf '  "nested_zip_prefix": "%s",\n' "$(json_escape "$NESTED_PREFIX")"
        printf '  "schema": 1,\n'
        printf '  "source_zip": "%s",\n' "$(json_escape "$ZIP_NAME")"
        printf '  "source_zip_sha256": "%s",\n' "$ZIP_SHA"
        printf '  "total_entries": %d,\n' "$TOTAL"
        printf '  "entries": [\n'
        FIRST=1
        while IFS= read -r line; do
            if [ "$FIRST" = "1" ]; then
                printf '    %s' "$line"
                FIRST=0
            else
                printf ',\n    %s' "$line"
            fi
        done < "$ENTRIES_FILE"
        printf '\n  ]\n}\n'
    } > "$LOCK"

    printf 'wrote lock: %s\n' "$LOCK"
    printf 'entries: %d\n' "$TOTAL"
}

# ──────────────────────────────────────────────────────────────────────────────
# extract: extract corpus using the lock file
# ──────────────────────────────────────────────────────────────────────────────
cmd_extract() {
    ZIP=""
    LOCK=""
    OUT_DIR=""

    while [ $# -gt 0 ]; do
        case "$1" in
            --zip)      ZIP="$2"; shift 2 ;;
            --lock)     LOCK="$2"; shift 2 ;;
            --out-dir)  OUT_DIR="$2"; shift 2 ;;
            *) die "unknown option: $1" ;;
        esac
    done

    [ -n "$ZIP" ]     || die "--zip required"
    [ -n "$LOCK" ]    || die "--lock required"
    [ -n "$OUT_DIR" ] || die "--out-dir required"
    [ -f "$ZIP" ]     || die "zip not found: $ZIP"
    [ -f "$LOCK" ]    || die "lock not found: $LOCK"

    TMPDIR_WORK=$(mktemp -d)
    trap 'rm -rf "$TMPDIR_WORK"' EXIT

    unzip -q "$ZIP" -d "$TMPDIR_WORK/outer"

    # extract outer entries
    find "$TMPDIR_WORK/outer" -type f | sort | while IFS= read -r f; do
        REL="${f#$TMPDIR_WORK/outer/}"
        EXT=$(printf '%s' "$REL" | sed 's/.*\.//' | tr '[:upper:]' '[:lower:]')
        case ".$EXT" in
            .cst|.csp|.csmap|.xsd|.xml|.json) ;;
            *) continue ;;
        esac
        DST="$OUT_DIR/outer/$REL"
        mkdir -p "$(dirname "$DST")"
        cp "$f" "$DST"
    done

    # extract nested zip entries
    NESTED_PREFIX=$(grep -o '"nested_zip_prefix"[[:space:]]*:[[:space:]]*"[^"]*"' "$LOCK" 2>/dev/null \
        | sed 's/.*"nested_zip_prefix"[[:space:]]*:[[:space:]]*"//; s/".*$//' || printf 'Samples')
    find "$TMPDIR_WORK/outer/$NESTED_PREFIX" -name '*.zip' -type f 2>/dev/null | sort | while IFS= read -r nzip; do
        NREL="${nzip#$TMPDIR_WORK/outer/}"
        NTMP="$TMPDIR_WORK/nested_$(basename "$nzip" .zip)"
        mkdir -p "$NTMP"
        unzip -q "$nzip" -d "$NTMP" 2>/dev/null || continue
        find "$NTMP" -type f | sort | while IFS= read -r f; do
            REL="${f#$NTMP/}"
            EXT=$(printf '%s' "$REL" | sed 's/.*\.//' | tr '[:upper:]' '[:lower:]')
            case ".$EXT" in
                .cst|.csp|.csmap|.xsd|.xml|.json) ;;
                *) continue ;;
            esac
            DST="$OUT_DIR/nested/$NREL/$REL"
            mkdir -p "$(dirname "$DST")"
            cp "$f" "$DST"
        done
    done

    TOTAL=$(find "$OUT_DIR" -type f | wc -l)
    printf 'extracted corpus: %s\n' "$OUT_DIR"
    printf 'files: %d\n' "$TOTAL"
}

# ──────────────────────────────────────────────────────────────────────────────
# dispatch
# ──────────────────────────────────────────────────────────────────────────────
CMD="${1:-}"
[ -n "$CMD" ] || { printf 'usage: %s {inventory|extract} [options]\n' "$0" >&2; exit 1; }
shift

case "$CMD" in
    inventory) cmd_inventory "$@" ;;
    extract)   cmd_extract   "$@" ;;
    *) die "unknown command: $CMD (expected inventory or extract)" ;;
esac
