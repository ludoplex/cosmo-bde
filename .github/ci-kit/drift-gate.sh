#!/bin/sh
# drift-gate.sh — codegen drift gate for generated manifest files (catalog §17)
# Regenerates the manifests, then FAILS if regeneration changed anything:
#   1. sha256 compare before/after regeneration (git-free; catches adds/removes)
#   2. git diff --exit-code + untracked check on generated paths (in a worktree)
# Usage: drift-gate.sh [-C dir] [-g cmd] [-m root_manifest] [-s submanifest] [-q] [extra_generated_file ...]
# Exit codes: 0 clean · 1 drift · 2 usage · 3 missing dep
# Prologue follows ~/bin/templates/module.sh.tmpl adapted to POSIX sh
# (set -eu instead of bash-only set -Eeuo pipefail + ERR trap).
set -eu

NAME=drift-gate.sh

# --- arena: every temp allocation lives under $ARENA; freed once at exit ---
ARENA=$(mktemp -d "${TMPDIR:-/tmp}/${NAME}.XXXXXX")
trap 'rm -rf "$ARENA"' EXIT

# --- helpers ---
die()   { printf '[%s] %s\n' "$NAME" "$1" >&2; exit "${2:-1}"; }
log()   { [ "$QUIET" = 1 ] || printf '[%s] %s\n' "$NAME" "$*" >&2; }
usage() { grep '^# Usage:' "$0" | cut -c3-; exit 2; }

# --- parameters: env-overridable defaults; NO literals in the body ---
: "${ROOT_MANIFEST:=FUNCTION_MANIFEST.md}"
: "${SUBMANIFEST:=FUNCTION_SUBMANIFEST.md}"
: "${GEN_CMD:=}"
: "${REPO:=.}"
QUIET=0

while getopts C:g:m:s:qh opt; do case $opt in
  C) REPO=$OPTARG ;;
  g) GEN_CMD=$OPTARG ;;
  m) ROOT_MANIFEST=$OPTARG ;;
  s) SUBMANIFEST=$OPTARG ;;
  q) QUIET=1 ;;
  *) usage ;;
esac; done
shift $((OPTIND - 1))

# --- preconditions ---
kit_dir=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd) || die "cannot resolve kit dir" 1
[ -d "$REPO" ] || die "not a directory: $REPO" 2
if command -v shasum >/dev/null 2>&1; then SHA='shasum -a 256'
elif command -v sha256sum >/dev/null 2>&1; then SHA=sha256sum
else die "missing dependency: shasum or sha256sum" 3; fi
if [ -z "$GEN_CMD" ]; then
  command -v python3 >/dev/null 2>&1 || die "missing dependency: python3 (or pass -g)" 3
  GEN_CMD="python3 '$kit_dir/manifest-gen.py' ."
fi
cd "$REPO" || die "cannot cd to $REPO" 1

# hash_generated: sha256 of every generated manifest file plus the extra
# generated files named as positional args, in a stable order.
hash_generated() {
  find . \( -name "$ROOT_MANIFEST" -o -name "$SUBMANIFEST" \) -type f \
      ! -path './.git/*' -print | LC_ALL=C sort |
    while IFS= read -r f; do $SHA "$f"; done
  for extra do
    [ -f "$extra" ] && $SHA "$extra" || printf 'MISSING  %s\n' "$extra"
  done
}

# --- gate ---
hash_generated "$@" > "$ARENA/before"
log "regenerating: $GEN_CMD"
sh -c "$GEN_CMD" > "$ARENA/gen.out" 2>&1 || {
  cat "$ARENA/gen.out" >&2
  die "generator failed: $GEN_CMD" 1
}
[ "$QUIET" = 1 ] || cat "$ARENA/gen.out" >&2
hash_generated "$@" > "$ARENA/after"

status=0
if ! diff -u "$ARENA/before" "$ARENA/after" > "$ARENA/drift"; then
  printf '[%s] DRIFT: regeneration changed generated files:\n' "$NAME" >&2
  cat "$ARENA/drift" >&2
  status=1
fi

if git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  if ! git diff --exit-code -- ":(glob)**/$ROOT_MANIFEST" ":(glob)**/$SUBMANIFEST" "$@"; then
    printf '[%s] DRIFT: generated files differ from the committed copies.\n' "$NAME" >&2
    status=1
  fi
  untracked=$(git ls-files --others --exclude-standard -- \
    ":(glob)**/$ROOT_MANIFEST" ":(glob)**/$SUBMANIFEST" "$@")
  if [ -n "$untracked" ]; then
    printf '[%s] DRIFT: generated files are not committed:\n%s\n' "$NAME" "$untracked" >&2
    status=1
  fi
fi

# --- postconditions ---
[ "$status" = 0 ] && log "clean: manifests are up to date"
exit "$status"
