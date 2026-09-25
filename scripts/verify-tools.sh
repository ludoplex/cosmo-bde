#!/bin/sh
# ═══════════════════════════════════════════════════════════════════════════
# verify-tools.sh - Verify all tools are available and working
# ═══════════════════════════════════════════════════════════════════════════
#
# cosmo-bde — BDE with Models
#
# Checks each ring's tools and reports status.
# Use this to verify a system is ready for development.
#
# Exit codes:
#   0 - All Ring 0 tools present (minimum viable)
#   1 - Missing Ring 0 tools (cannot build)
#
# ═══════════════════════════════════════════════════════════════════════════

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"

# Colors
if [ -t 1 ]; then
    RED='\033[0;31m'
    GREEN='\033[0;32m'
    YELLOW='\033[1;33m'
    CYAN='\033[0;36m'
    BOLD='\033[1m'
    NC='\033[0m'
else
    RED='' GREEN='' YELLOW='' CYAN='' BOLD='' NC=''
fi

RING0_OK=1
RING1_OK=1
RING2_OK=1

check_cmd() {
    name="$1"
    cmd="$2"

    if command -v "$cmd" >/dev/null 2>&1; then
        printf "  ${GREEN}✓${NC} %-20s %s\n" "$name" "$(command -v "$cmd")"
        return 0
    else
        printf "  ${RED}✗${NC} %-20s (not found)\n" "$name"
        return 1
    fi
}

check_build() {
    name="$1"
    path="$2"

    if [ -x "$BUILD_DIR/$path" ]; then
        printf "  ${GREEN}✓${NC} %-20s %s\n" "$name" "$BUILD_DIR/$path"
        return 0
    else
        printf "  ${YELLOW}○${NC} %-20s (run: make tools)\n" "$name"
        return 1
    fi
}

check_dir() {
    name="$1"
    path="$2"

    if [ -d "$ROOT_DIR/$path" ]; then
        printf "  ${GREEN}✓${NC} %-20s %s\n" "$name" "$path"
        return 0
    else
        printf "  ${YELLOW}○${NC} %-20s (not present)\n" "$name"
        return 1
    fi
}

echo "═══════════════════════════════════════════════════════════════════════"
echo " cosmo-bde — Tool Verification"
echo "═══════════════════════════════════════════════════════════════════════"

# ── Ring 0: Bootstrap ──────────────────────────────────────────────────────

echo
printf "${CYAN}${BOLD}Ring 0: Bootstrap${NC} (C + sh + make — always required)\n"
echo "─────────────────────────────────────────────────────────────────────────"

echo "System tools:"
check_cmd "C Compiler" "cc" || check_cmd "C Compiler" "gcc" || check_cmd "C Compiler" "clang" || RING0_OK=0
check_cmd "Make" "make" || RING0_OK=0
check_cmd "Shell" "sh" || RING0_OK=0
check_cmd "Git" "git" || true

echo
echo "In-tree generators (tools/):"
check_build "schemagen" "schemagen" || true
check_build "lemon" "lemon" || true
check_build "smgen" "smgen" || true
check_build "bddgen" "bddgen" || true

echo
echo "Vendored libraries (vendors/libs/):"
check_dir "SQLite (cosmo)" "vendors/submodules/cosmopolitan/third_party/sqlite3" || true
check_dir "yyjson" "vendors/libs/yyjson" || true
check_dir "Nuklear" "vendors/libs/nuklear" || true

echo
echo "APE toolchain:"
if check_cmd "cosmocc" "cosmocc"; then
    if [ -n "$(cosmocc --version 2>&1 | head -1)" ]; then
        printf "    Version: %s\n" "$(cosmocc --version 2>&1 | head -1)"
    fi
else
    printf "    ${YELLOW}Install:${NC} https://cosmo.zip/pub/cosmocc/\n"
fi

# ── Ring 1: Velocity Tools ─────────────────────────────────────────────────

echo
printf "${CYAN}${BOLD}Ring 1: Velocity Tools${NC} (optional, enhance development)\n"
echo "─────────────────────────────────────────────────────────────────────────"

echo "Code quality:"
check_cmd "gengetopt" "gengetopt" || true
check_cmd "cppcheck" "cppcheck" || RING1_OK=0
check_cmd "valgrind" "valgrind" || true

check_build "makeheaders" "makeheaders" || true

echo
echo "Compiler sanitizers:"
if cc -fsanitize=address -x c -c /dev/null -o /dev/null 2>/dev/null; then
    printf "  ${GREEN}✓${NC} AddressSanitizer\n"
else
    printf "  ${YELLOW}○${NC} AddressSanitizer (not supported)\n"
fi
if cc -fsanitize=undefined -x c -c /dev/null -o /dev/null 2>/dev/null; then
    printf "  ${GREEN}✓${NC} UndefinedBehaviorSan\n"
else
    printf "  ${YELLOW}○${NC} UndefinedBehaviorSan (not supported)\n"
fi
if cc -fsanitize=thread -x c -c /dev/null -o /dev/null 2>/dev/null; then
    printf "  ${GREEN}✓${NC} ThreadSanitizer\n"
else
    printf "  ${YELLOW}○${NC} ThreadSanitizer (not supported)\n"
fi

# ── Ring 2: External Toolchains ────────────────────────────────────────────

echo
printf "${CYAN}${BOLD}Ring 2: External Toolchains${NC} (outputs committed to gen/imported/)\n"
echo "─────────────────────────────────────────────────────────────────────────"

echo "FOSS tools:"
if check_cmd ".NET SDK" "dotnet"; then
    printf "    ${YELLOW}→${NC} StateSmith available\n"
else
    RING2_OK=0
fi
check_cmd "protoc" "protoc" || true
check_cmd "flatcc" "flatcc" || true
check_cmd "OpenModelica" "omc" || true

echo
echo "Commercial tools:"
check_cmd "MATLAB" "matlab" || true
check_cmd "Rhapsody" "rhapsodycl" || true

echo
echo "Model directories:"
check_dir "statesmith" "model/statesmith" || true
check_dir "simulink" "model/simulink" || true
check_dir "openmodelica" "model/openmodelica" || true
check_dir "rhapsody" "model/rhapsody" || true

# ── schemagen v2.0.0 Composability ─────────────────────────────────────────

echo
printf "${CYAN}${BOLD}schemagen v2.0.0 Composability${NC}\n"
echo "─────────────────────────────────────────────────────────────────────────"

if [ -x "$BUILD_DIR/schemagen" ]; then
    version=$("$BUILD_DIR/schemagen" --help 2>&1 | head -1 | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' || echo "unknown")
    printf "  ${GREEN}✓${NC} schemagen version: %s\n" "$version"
    echo "    Output formats:"
    echo "      --c      C types, init, validate"
    echo "      --json   JSON serialization (yyjson)"
    echo "      --sql    SQLite bindings"
    echo "      --proto  Protocol Buffers .proto"
    echo "      --fbs    FlatBuffers .fbs"
    echo "      --all    All formats (full composability)"
else
    printf "  ${YELLOW}○${NC} schemagen not built (run: make tools)\n"
fi

# ── Summary ────────────────────────────────────────────────────────────────

echo
echo "═══════════════════════════════════════════════════════════════════════"
echo " Summary"
echo "═══════════════════════════════════════════════════════════════════════"

if [ "$RING0_OK" = "1" ]; then
    printf "${GREEN}Ring 0: READY${NC} — Bootstrap build possible\n"
else
    printf "${RED}Ring 0: MISSING${NC} — Cannot build without C compiler, make, sh\n"
fi

if [ "$RING1_OK" = "1" ]; then
    printf "${GREEN}Ring 1: AVAILABLE${NC} — Enhanced development experience\n"
else
    printf "${YELLOW}Ring 1: PARTIAL${NC} — Some velocity tools missing (optional)\n"
fi

printf "${YELLOW}Ring 2: "
if [ "$RING2_OK" = "1" ]; then
    printf "AVAILABLE${NC} — External toolchains detected\n"
else
    printf "COMMITTED${NC} — Tool outputs must already be in gen/imported/\n"
fi

echo
echo "Composability chain:"
echo "  .schema → schemagen --all → C, JSON, SQL, .proto, .fbs"
echo "                              ↓        ↓       ↓"
echo "                           Native   protoc  flatcc"
echo "                              ↓        ↓       ↓"
echo "                           cosmocc   ───────────"
echo "                              ↓"
echo "                           APE binary"
echo

if [ "$RING0_OK" = "1" ]; then
    echo "System is ready for cosmo-bde development."
    exit 0
else
    echo "Install missing Ring 0 tools before proceeding."
    exit 1
fi
