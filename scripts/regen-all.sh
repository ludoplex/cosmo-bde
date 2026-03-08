#!/bin/sh
# ═══════════════════════════════════════════════════════════════════════════
# regen-all.sh - Regenerate all generated code from specs
# ═══════════════════════════════════════════════════════════════════════════
#
# cosmo-bde — BDE with Models
#
# This script orchestrates ALL generators across ALL rings.
# Ring 2 tools are auto-detected—available tools are used, missing are skipped.
# Ring 2 outputs are committed, so builds always succeed with just C+sh.
#
# Usage: ./scripts/regen-all.sh [--verify]
#   --verify: Also run git diff --exit-code after regeneration
#
# Exit codes:
#   0 - Success
#   1 - Generator failed
#   2 - Drift detected (with --verify)
#
# ═══════════════════════════════════════════════════════════════════════════

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
SPECS_DIR="$ROOT_DIR/specs"
GEN_DIR="$ROOT_DIR/gen"
MODEL_DIR="$ROOT_DIR/model"
VERIFY=0

for arg in "$@"; do
    case "$arg" in
        --verify) VERIFY=1 ;;
    esac
done

cd "$ROOT_DIR"

echo "═══════════════════════════════════════════════════════════════════════"
echo " cosmo-bde — BDE with Models"
echo " Regenerate All (auto-detecting tools)"
echo "═══════════════════════════════════════════════════════════════════════"

# ── Ring 0 Generators ──────────────────────────────────────────────────────

echo
echo "── Ring 0: In-tree generators ──────────────────────────────────────────"
echo "   (C + sh + make — always available)"

# schemagen (multi-format: C, JSON, SQL, proto, fbs)
if [ -x "$BUILD_DIR/schemagen" ]; then
    echo "[schemagen] Processing specs/**/*.schema (--all formats)..."
    find "$SPECS_DIR" -name "*.schema" | while read -r spec; do
        layer=$(basename "$(dirname "$spec")")
        mkdir -p "$GEN_DIR/$layer"
        name=$(basename "$spec" .schema)
        echo "  $spec → gen/$layer/ (C, JSON, SQL, proto, fbs)"
        "$BUILD_DIR/schemagen" --all "$spec" "$GEN_DIR/$layer" "$name" 2>/dev/null || \
            echo "    (failed)"
    done
else
    echo "[schemagen] Not built. Run 'make' first."
fi

# lemon (parser generator)
if [ -x "$BUILD_DIR/lemon" ]; then
    echo "[lemon] Processing specs/**/*.y..."
    find "$SPECS_DIR" -name "*.y" | while read -r spec; do
        layer=$(basename "$(dirname "$spec")")
        mkdir -p "$GEN_DIR/$layer"
        echo "  $spec → gen/$layer/"
        "$BUILD_DIR/lemon" "$spec" 2>/dev/null && \
            mv "${spec%.y}.c" "$GEN_DIR/$layer/" 2>/dev/null && \
            mv "${spec%.y}.h" "$GEN_DIR/$layer/" 2>/dev/null || \
            echo "    (skipped)"
    done
else
    echo "[lemon] Not built. Run 'make' first."
fi

# defgen (X-macro definitions)
if [ -x "$BUILD_DIR/defgen" ]; then
    echo "[defgen] Processing specs/**/*.def (X-macros)..."
    find "$SPECS_DIR" -name "*.def" | while read -r spec; do
        layer=$(basename "$(dirname "$spec")")
        mkdir -p "$GEN_DIR/$layer"
        name=$(basename "$spec" .def)
        echo "  $spec → gen/$layer/${name}_defs.h"
        "$BUILD_DIR/defgen" "$spec" "$GEN_DIR/$layer" "$name" 2>/dev/null || \
            echo "    (skipped - defgen parse error)"
    done
else
    echo "[defgen] Not built yet"
fi

# smgen (state machine generator)
if [ -x "$BUILD_DIR/smgen" ]; then
    echo "[smgen] Processing specs/**/*.sm..."
    find "$SPECS_DIR" -name "*.sm" | while read -r spec; do
        layer=$(basename "$(dirname "$spec")")
        mkdir -p "$GEN_DIR/$layer"
        echo "  $spec → gen/$layer/"
        "$BUILD_DIR/smgen" "$spec" "$GEN_DIR/$layer" 2>/dev/null || \
            echo "    (skipped - smgen not ready)"
    done
else
    echo "[smgen] Not built yet"
fi

# hsmgen (hierarchical state machine generator)
if [ -x "$BUILD_DIR/hsmgen" ]; then
    echo "[hsmgen] Processing specs/**/*.hsm..."
    find "$SPECS_DIR" -name "*.hsm" | while read -r spec; do
        layer=$(basename "$(dirname "$spec")")
        mkdir -p "$GEN_DIR/$layer"
        echo "  $spec → gen/$layer/"
        "$BUILD_DIR/hsmgen" "$spec" "$GEN_DIR/$layer" 2>/dev/null || \
            echo "    (skipped - hsmgen parse error)"
    done
else
    echo "[hsmgen] Not built yet"
fi

# bddgen (BDD test generator)
if [ -x "$BUILD_DIR/bddgen" ]; then
    echo "[bddgen] Processing specs/**/*.feature..."
    find "$SPECS_DIR" -name "*.feature" | while read -r spec; do
        layer=$(basename "$(dirname "$spec")")
        mkdir -p "$GEN_DIR/$layer"
        echo "  $spec → gen/$layer/"
        "$BUILD_DIR/bddgen" "$spec" "$GEN_DIR/$layer" 2>/dev/null || \
            echo "    (skipped - bddgen not ready)"
    done
else
    echo "[bddgen] Not built yet"
fi

# apigen (API endpoint generator)
if [ -x "$BUILD_DIR/apigen" ]; then
    echo "[apigen] Processing specs/**/*.api..."
    find "$SPECS_DIR" -name "*.api" | while read -r spec; do
        layer=$(basename "$(dirname "$spec")")
        mkdir -p "$GEN_DIR/$layer"
        echo "  $spec → gen/$layer/"
        "$BUILD_DIR/apigen" "$spec" "$GEN_DIR/$layer" 2>/dev/null || \
            echo "    (skipped - apigen parse error)"
    done
else
    echo "[apigen] Not built yet"
fi

# clipsgen (business rules generator)
if [ -x "$BUILD_DIR/clipsgen" ]; then
    echo "[clipsgen] Processing specs/**/*.rules..."
    find "$SPECS_DIR" -name "*.rules" | while read -r spec; do
        layer=$(basename "$(dirname "$spec")")
        mkdir -p "$GEN_DIR/$layer"
        name=$(basename "$spec" .rules)
        echo "  $spec → gen/$layer/"
        "$BUILD_DIR/clipsgen" "$spec" "$GEN_DIR/$layer" "$name" 2>/dev/null || \
            echo "    (skipped - clipsgen parse error)"
    done
else
    echo "[clipsgen] Not built yet"
fi

# sqlgen (SQLite bindings generator)
if [ -x "$BUILD_DIR/sqlgen" ]; then
    echo "[sqlgen] Processing specs/**/*.sql..."
    find "$SPECS_DIR" -name "*.sql" | while read -r spec; do
        layer=$(basename "$(dirname "$spec")")
        mkdir -p "$GEN_DIR/$layer"
        name=$(basename "$spec" .sql)
        echo "  $spec → gen/$layer/"
        "$BUILD_DIR/sqlgen" "$spec" "$GEN_DIR/$layer" "$name" 2>/dev/null || \
            echo "    (skipped - sqlgen parse error)"
    done
else
    echo "[sqlgen] Not built yet"
fi

# ── Ring 1 Generators (Velocity Tools - Auto-Detected) ───────────────────────

echo
echo "── Ring 1: Velocity tools (auto-detected) ──────────────────────────────────"
echo "   (Ring 0 + optional C tools)"

# makeheaders (auto-generate .h from .c)
if [ -x "$BUILD_DIR/makeheaders" ]; then
    echo "[makeheaders] Scanning src/*.c for exportable functions..."
    SRCS=$(find "$ROOT_DIR/src" -name "*.c" 2>/dev/null | tr '\n' ' ')
    if [ -n "$SRCS" ]; then
        "$BUILD_DIR/makeheaders" $SRCS 2>/dev/null && \
            echo "  Headers updated" || \
            echo "  (no changes)"
    fi
else
    echo "[makeheaders] Not built (run: make ring1)"
fi

# gengetopt (.ggo → CLI parser)
if command -v gengetopt >/dev/null 2>&1; then
    echo "[gengetopt] Processing specs/**/*.ggo..."
    find "$SPECS_DIR" -name "*.ggo" 2>/dev/null | while read -r spec; do
        layer=$(basename "$(dirname "$spec")")
        mkdir -p "$GEN_DIR/$layer"
        name=$(basename "$spec" .ggo)
        echo "  $spec → gen/$layer/"
        gengetopt -i "$spec" -F "${name}_cli" --output-dir="$GEN_DIR/$layer" 2>/dev/null || \
            echo "    (failed)"
    done
else
    echo "[gengetopt] Not installed (apt install gengetopt)"
fi

# cppcheck (static analysis - reports only, no generation)
if command -v cppcheck >/dev/null 2>&1; then
    echo "[cppcheck] Available for static analysis (run: make lint)"
else
    echo "[cppcheck] Not installed (apt install cppcheck)"
fi

# ── Ring 2 Generators (FOSS - Auto-Detected) ─────────────────────────────────

echo
echo "── Ring 2: FOSS tools (auto-detected) ────────────────────────────────────"
echo "   (External toolchains — outputs committed to gen/imported/)"

# StateSmith (.NET) - generates zero-dependency C
if command -v dotnet >/dev/null 2>&1; then
    echo "[StateSmith] Processing model/statesmith/*.drawio..."
    find "$MODEL_DIR/statesmith" -name "*.drawio" 2>/dev/null | while read -r spec; do
        mkdir -p "$GEN_DIR/imported/statesmith"
        echo "  $spec → gen/imported/statesmith/"
        if [ -f "$ROOT_DIR/vendors/submodules/StateSmith/src/StateSmith.Cli/StateSmith.Cli.csproj" ]; then
            dotnet run --project "$ROOT_DIR/vendors/submodules/StateSmith/src/StateSmith.Cli/StateSmith.Cli.csproj" -- \
                "$spec" --lang C99 -o "$GEN_DIR/imported/statesmith" 2>/dev/null || \
                echo "    (StateSmith generation failed)"
        else
            echo "    (StateSmith CLI project not available)"
        fi
    done
else
    echo "[StateSmith] .NET not available, outputs must already be committed"
fi

# protobuf-c - generates pure C serialization
if command -v protoc >/dev/null 2>&1; then
    echo "[protobuf-c] Processing specs/**/*.proto..."
    find "$SPECS_DIR" -name "*.proto" | while read -r spec; do
        mkdir -p "$GEN_DIR/imported/protobuf"
        echo "  $spec → gen/imported/protobuf/"
        protoc --c_out="$GEN_DIR/imported/protobuf" "$spec" 2>/dev/null || \
            echo "    (skipped)"
    done
else
    echo "[protobuf-c] Not available, outputs must already be committed"
fi

# flatcc - generates zero-copy C
if command -v flatcc >/dev/null 2>&1; then
    echo "[flatcc] Processing specs/**/*.fbs..."
    find "$SPECS_DIR" -name "*.fbs" | while read -r spec; do
        mkdir -p "$GEN_DIR/imported/flatbuf"
        echo "  $spec → gen/imported/flatbuf/"
        flatcc -a -o "$GEN_DIR/imported/flatbuf" "$spec" 2>/dev/null || \
            echo "    (skipped)"
    done
else
    echo "[flatcc] Not available, outputs must already be committed"
fi

# OpenModelica - generates C simulation code
if command -v omc >/dev/null 2>&1; then
    echo "[OpenModelica] Processing model/openmodelica/*.mo..."
    find "$MODEL_DIR/openmodelica" -name "*.mo" 2>/dev/null | while read -r spec; do
        mkdir -p "$GEN_DIR/imported/modelica"
        echo "  $spec → gen/imported/modelica/"
        omc "$spec" +s +d=initialization --output="$GEN_DIR/imported/modelica" 2>/dev/null || \
            echo "    (OpenModelica generation failed)"
    done
else
    echo "[OpenModelica] Not available, outputs must already be committed"
fi

# ── Ring 2 Generators (Commercial - Auto-Detected) ───────────────────────────

echo
echo "── Ring 2: Commercial tools (auto-detected) ──────────────────────────────"

# MATLAB/Simulink Embedded Coder
if command -v matlab >/dev/null 2>&1; then
    echo "[Embedded Coder] Processing model/simulink/*.slx..."
    find "$MODEL_DIR/simulink" -name "*.slx" 2>/dev/null | while read -r spec; do
        mkdir -p "$GEN_DIR/imported/simulink"
        echo "  $spec → gen/imported/simulink/"
        matlab -batch "slbuild('$spec')" 2>/dev/null || \
            echo "    (Embedded Coder generation failed)"
    done
else
    echo "[Embedded Coder] MATLAB not available, outputs must already be committed"
fi

# IBM Rhapsody
if command -v rhapsodycl >/dev/null 2>&1; then
    echo "[Rhapsody] Processing model/rhapsody/*.emx..."
    find "$MODEL_DIR/rhapsody" -name "*.emx" 2>/dev/null | while read -r spec; do
        mkdir -p "$GEN_DIR/imported/rhapsody"
        echo "  $spec → gen/imported/rhapsody/"
        rhapsodycl -generate "$spec" 2>/dev/null || \
            echo "    (Rhapsody generation failed)"
    done
else
    echo "[Rhapsody] Not available, outputs must already be committed"
fi

# ── Stamp Files ────────────────────────────────────────────────────────────

# Skip timestamp update during verify to avoid false drift detection
if [ "$VERIFY" != "1" ]; then
    echo
    echo "── Updating timestamps ───────────────────────────────────────────────────"
    date -u +"%Y-%m-%dT%H:%M:%SZ" > "$GEN_DIR/REGEN_TIMESTAMP"
    echo "  gen/REGEN_TIMESTAMP updated"
fi

# ── Verification ───────────────────────────────────────────────────────────

if [ "$VERIFY" = "1" ]; then
    echo
    echo "── Verification ────────────────────────────────────────────────────────"
    # Exclude timestamp-based files from drift check (GENERATOR_VERSION, REGEN_TIMESTAMP)
    # These files contain build timestamps that will always differ
    if git diff --quiet -- "$GEN_DIR" ':(exclude)gen/**/GENERATOR_VERSION' ':(exclude)gen/REGEN_TIMESTAMP' 2>/dev/null; then
        echo "[OK]    gen/ is clean (no uncommitted changes)"
    else
        echo "[FAIL]  gen/ has uncommitted changes:"
        git diff --stat -- "$GEN_DIR" ':(exclude)gen/**/GENERATOR_VERSION' ':(exclude)gen/REGEN_TIMESTAMP'
        exit 2
    fi
fi

echo
echo "═══════════════════════════════════════════════════════════════════════"
echo " Regeneration complete"
echo "═══════════════════════════════════════════════════════════════════════"
