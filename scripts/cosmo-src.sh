#!/bin/sh
# cosmo-src.sh — sparse, shallow checkout of the jart/cosmopolitan sources that
# cosmo-bde reuses (third_party/sqlite3 plus the one header it needs that the
# cosmocc toolchain does not ship), at the commit pinned by the
# vendors/submodules/cosmopolitan gitlink (= cosmocc.mk COSMO_SRC_COMMIT).
# Usage: scripts/cosmo-src.sh [DIR]      (default: vendors/submodules/cosmopolitan)
# Exit codes: 0 ok · 1 failure · 3 missing dependency
# Prologue follows the module.sh template adapted to POSIX sh (set -eu).
set -eu

NAME=cosmo-src.sh

# --- arena: every temp allocation lives under $ARENA; freed once at exit ---
ARENA=$(mktemp -d "${TMPDIR:-/tmp}/${NAME}.XXXXXX")
trap 'rm -rf "$ARENA"' EXIT

die() { printf '[%s] %s\n' "$NAME" "$1" >&2; exit "${2:-1}"; }

# --- parameters: env-overridable defaults ---
: "${COSMO_URL:=https://github.com/jart/cosmopolitan.git}"
DIR=${1:-vendors/submodules/cosmopolitan}

# --- preconditions ---
command -v git >/dev/null 2>&1 || die "missing dependency: git" 3
cd "$(dirname "$0")/.." || die "cannot cd to repo root"
SHA=$(git ls-files -s -- "$DIR" | awk '$1 == "160000" { print $2 }')
[ -n "$SHA" ] || die "no gitlink for $DIR in the index"

if [ -f "$DIR/third_party/sqlite3/sqlite3.h" ] &&
   [ "$(git -C "$DIR" rev-parse HEAD 2>/dev/null)" = "$SHA" ]; then
  printf '%s already at %s\n' "$DIR" "$SHA"
  exit 0
fi

# --- fetch exactly the pinned commit, only the paths we build from ---
mkdir -p "$DIR"
git -C "$DIR" init -q
git -C "$DIR" remote get-url origin >/dev/null 2>&1 ||
  git -C "$DIR" remote add origin "$COSMO_URL"
git -C "$DIR" sparse-checkout set --no-cone /third_party/sqlite3/ /libc/bsdstdlib.h
git -C "$DIR" fetch -q --depth 1 --filter=blob:none origin "$SHA" > "$ARENA/fetch.log" 2>&1 ||
  { cat "$ARENA/fetch.log" >&2; die "fetch of $SHA from $COSMO_URL failed"; }
git -C "$DIR" checkout -q --detach "$SHA"

# --- postconditions ---
[ "$(git -C "$DIR" rev-parse HEAD)" = "$SHA" ] || die "checkout is not at $SHA"
[ -f "$DIR/third_party/sqlite3/sqlite3.h" ] || die "third_party/sqlite3 missing after checkout"
printf '%s at %s (sparse: third_party/sqlite3, libc/bsdstdlib.h)\n' "$DIR" "$SHA"
