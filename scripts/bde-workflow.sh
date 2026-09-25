#!/bin/sh
# ══════════════════════════════════════════════════════════════════════════════
# bde-workflow.sh - Behavior Driven Engineering Workflow
# ══════════════════════════════════════════════════════════════════════════════
#
# cosmo-bde — BDE with Models
#
# This script implements the complete BDE workflow:
#   1. Feature-first: Start with .feature specs (behavior)
#   2. Model-driven: Generate from .schema, .sm, .drawio (structure + behavior)
#   3. Code-generated: Produce C code from specs
#   4. Test-verified: BDD tests verify behavior matches spec
#   5. Actually-portable: Compile with cosmocc to APE
#
# Usage:
#   ./scripts/bde-workflow.sh new <layer> <name>     # Create new feature
#   ./scripts/bde-workflow.sh regen                   # Regenerate all
#   ./scripts/bde-workflow.sh verify                  # Verify drift + tests
#   ./scripts/bde-workflow.sh build [ape]             # Build (optionally APE)
#   ./scripts/bde-workflow.sh full                    # Complete workflow
#
# ══════════════════════════════════════════════════════════════════════════════

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'

cd "$ROOT_DIR"

# POSIX-compatible capitalize first letter
capitalize() {
    echo "$1" | awk '{ print toupper(substr($0,1,1)) tolower(substr($0,2)) }'
}

# ── Banner ───────────────────────────────────────────────────────────────────

banner() {
    echo ""
    echo "${CYAN}╔══════════════════════════════════════════════════════════════════════════╗${NC}"
    echo "${CYAN}║${NC}  ${BOLD}BDE with Models${NC}                                                        ${CYAN}║${NC}"
    echo "${CYAN}║${NC}  Behavior Driven Engineering                                             ${CYAN}║${NC}"
    echo "${CYAN}║${NC}  cosmo-bde                                                         ${CYAN}║${NC}"
    echo "${CYAN}╚══════════════════════════════════════════════════════════════════════════╝${NC}"
    echo ""
}

# ── Commands ─────────────────────────────────────────────────────────────────

cmd_new() {
    layer="${1:-domain}"
    name="${2:-untitled}"

    banner
    echo "${BOLD}Creating new BDE feature: ${layer}/${name}${NC}"
    echo ""

    # Step 1: Create .feature (behavior spec)
    echo "${CYAN}[1/4]${NC} Creating behavior spec (.feature)..."
    mkdir -p "specs/testing"
    cat > "specs/testing/${name}.feature" << EOF
# ═══════════════════════════════════════════════════════════════════════
# ${name}.feature — BDD Specification
# ═══════════════════════════════════════════════════════════════════════
#
# @layer    testing
# @entity   ${name}
# @author   $(git config user.name 2>/dev/null || echo "developer")
#
# ═══════════════════════════════════════════════════════════════════════

Feature: $(capitalize "$name") behavior

  As a developer
  I want ${name} to behave correctly
  So that the system is reliable

  Background:
    Given the system is initialized

  Scenario: $(capitalize "$name") initializes with defaults
    Given a new ${name}
    When I check its state
    Then it should be valid

  Scenario: $(capitalize "$name") validates constraints
    Given a ${name} with invalid data
    When I validate it
    Then validation should fail

  # Add more scenarios here...
EOF
    echo "  Created: specs/testing/${name}.feature"

    # Step 2: Create .schema (data model)
    echo "${CYAN}[2/4]${NC} Creating data model (.schema)..."
    mkdir -p "specs/${layer}"
    cat > "specs/${layer}/${name}.schema" << EOF
/* ═══════════════════════════════════════════════════════════════════════
 * ${name}.schema — Data Model
 * ═══════════════════════════════════════════════════════════════════════
 *
 * @layer    ${layer}
 * @version  0.1.0
 * @author   $(git config user.name 2>/dev/null || echo "developer")
 *
 * @produces gen/${layer}/${name}_types.c, gen/${layer}/${name}_types.h
 * @feature  specs/testing/${name}.feature
 *
 * ═══════════════════════════════════════════════════════════════════════
 */

type $(capitalize "$name") {
    id: u64 [doc: "Unique identifier"]
    name: string[64] [doc: "Display name", not_empty]
    enabled: i32 [doc: "Active flag", default: 1]
    /* Add more fields here... */
}
EOF
    echo "  Created: specs/${layer}/${name}.schema"

    # Step 3: Create .sm (state machine - if behavior layer)
    if [ "$layer" = "behavior" ]; then
        echo "${CYAN}[3/4]${NC} Creating state machine (.sm)..."
        cat > "specs/behavior/${name}.sm" << EOF
/* ═══════════════════════════════════════════════════════════════════════
 * ${name}.sm — State Machine
 * ═══════════════════════════════════════════════════════════════════════
 *
 * @layer    behavior
 * @version  0.1.0
 * @author   $(git config user.name 2>/dev/null || echo "developer")
 *
 * @produces gen/behavior/${name}_sm.c, gen/behavior/${name}_sm.h
 * @feature  specs/testing/${name}.feature
 *
 * ═══════════════════════════════════════════════════════════════════════
 */

machine $(capitalize "$name") {
    initial: Idle

    state Idle {
        on Start -> Running
    }

    state Running {
        on Stop -> Idle
        on Error -> Failed
    }

    state Failed {
        on Reset -> Idle
    }
}
EOF
        echo "  Created: specs/behavior/${name}.sm"
    else
        echo "${CYAN}[3/4]${NC} Skipping state machine (not behavior layer)"
    fi

    # Step 4: Instructions
    echo "${CYAN}[4/4]${NC} Next steps:"
    echo ""
    echo "  1. Edit the .feature file to define behavior"
    echo "  2. Edit the .schema file to define data model"
    echo "  3. Run: ${BOLD}make regen${NC}"
    echo "  4. Run: ${BOLD}make verify${NC}"
    echo "  5. Implement and test"
    echo ""
}

cmd_regen() {
    banner
    echo "${BOLD}Regenerating all code from specs...${NC}"
    echo ""

    # Build tools first
    echo "${CYAN}[1/3]${NC} Building Ring 0 generators..."
    make tools 2>&1 | grep -E "^(cc|Ring)" || true

    # Run regen
    echo ""
    echo "${CYAN}[2/3]${NC} Processing specs..."
    ./scripts/regen-all.sh

    # Show summary
    echo ""
    echo "${CYAN}[3/3]${NC} Summary:"
    echo "  Specs:     $(find specs -type f 2>/dev/null | wc -l) files"
    echo "  Generated: $(find gen -name "*.c" -o -name "*.h" 2>/dev/null | wc -l) files"
    echo ""
}

cmd_verify() {
    banner
    echo "${BOLD}Verifying BDE consistency...${NC}"
    echo ""

    ERRORS=0

    # Step 1: Regen
    echo "${CYAN}[1/4]${NC} Regenerating from specs..."
    make regen >/dev/null 2>&1 || true

    # Step 2: Check drift
    echo "${CYAN}[2/4]${NC} Checking for drift..."
    if git diff --quiet gen/ 2>/dev/null; then
        echo "  ${GREEN}✓${NC} gen/ is clean"
    else
        echo "  ${RED}✗${NC} gen/ has uncommitted changes"
        git diff --stat gen/ 2>/dev/null | head -5
        ERRORS=$((ERRORS + 1))
    fi

    # Step 3: Compile check
    echo "${CYAN}[3/4]${NC} Validating generated code compiles..."
    for c in $(find gen -name "*.c" 2>/dev/null); do
        dir=$(dirname "$c")
        if cc -c -I"$dir" "$c" -o /tmp/verify_$$.o 2>/dev/null; then
            rm -f /tmp/verify_$$.o
        else
            echo "  ${RED}✗${NC} $c failed to compile"
            ERRORS=$((ERRORS + 1))
        fi
    done
    [ "$ERRORS" -eq 0 ] && echo "  ${GREEN}✓${NC} All generated code compiles"

    # Step 4: BDD tests
    echo "${CYAN}[4/4]${NC} Running BDD tests..."
    if [ -x "$BUILD_DIR/bddgen" ]; then
        make test 2>/dev/null || ERRORS=$((ERRORS + 1))
    else
        echo "  ${YELLOW}○${NC} bddgen not built (skipping)"
    fi

    echo ""
    if [ "$ERRORS" -eq 0 ]; then
        echo "${GREEN}Verification passed${NC}"
        exit 0
    else
        echo "${RED}Verification failed ($ERRORS errors)${NC}"
        exit 1
    fi
}

cmd_build() {
    mode="${1:-native}"

    banner
    echo "${BOLD}Building application...${NC}"
    echo ""

    if [ "$mode" = "ape" ]; then
        echo "${CYAN}Building Actually Portable Executable (APE)...${NC}"
        if command -v cosmocc >/dev/null 2>&1; then
            CC=cosmocc make clean all
            echo ""
            echo "APE binary: build/app"
            file build/app
        else
            echo "${RED}Error: cosmocc not found${NC}"
            echo "Run: make toolchain   (pinned cosmocc, sha256-verified; see cosmocc.mk)"
            exit 1
        fi
    else
        echo "${CYAN}Building native executable...${NC}"
        make CC=cc clean all
        echo ""
        echo "Native binary: build/app"
        file build/app
    fi
}

cmd_full() {
    banner
    echo "${BOLD}Running full BDE workflow...${NC}"
    echo ""

    echo "${CYAN}═══════════════════════════════════════════════════════════════════════${NC}"
    echo " Step 1: Build Tools"
    echo "${CYAN}═══════════════════════════════════════════════════════════════════════${NC}"
    make tools

    echo ""
    echo "${CYAN}═══════════════════════════════════════════════════════════════════════${NC}"
    echo " Step 2: Regenerate from Specs"
    echo "${CYAN}═══════════════════════════════════════════════════════════════════════${NC}"
    make regen

    echo ""
    echo "${CYAN}═══════════════════════════════════════════════════════════════════════${NC}"
    echo " Step 3: Verify Consistency"
    echo "${CYAN}═══════════════════════════════════════════════════════════════════════${NC}"
    make verify || echo "(Continuing despite drift...)"

    echo ""
    echo "${CYAN}═══════════════════════════════════════════════════════════════════════${NC}"
    echo " Step 4: Build Application"
    echo "${CYAN}═══════════════════════════════════════════════════════════════════════${NC}"
    make app

    echo ""
    echo "${CYAN}═══════════════════════════════════════════════════════════════════════${NC}"
    echo " Step 5: Run Tests"
    echo "${CYAN}═══════════════════════════════════════════════════════════════════════${NC}"
    ./scripts/test.sh || true

    echo ""
    echo "${CYAN}═══════════════════════════════════════════════════════════════════════${NC}"
    echo " Step 6: Run Application"
    echo "${CYAN}═══════════════════════════════════════════════════════════════════════${NC}"
    make run

    echo ""
    echo "${GREEN}BDE workflow complete${NC}"
}

cmd_help() {
    banner
    echo "Usage: ./scripts/bde-workflow.sh <command> [args]"
    echo ""
    echo "Commands:"
    echo "  new <layer> <name>    Create new BDE feature (.feature + .schema + .sm)"
    echo "  regen                 Regenerate all code from specs"
    echo "  verify                Verify consistency (drift + compile + BDD)"
    echo "  build [ape]           Build application (native or APE)"
    echo "  full                  Run complete BDE workflow"
    echo "  help                  Show this help"
    echo ""
    echo "Examples:"
    echo "  ./scripts/bde-workflow.sh new domain sensor"
    echo "  ./scripts/bde-workflow.sh new behavior door"
    echo "  ./scripts/bde-workflow.sh full"
    echo ""
    echo "BDE Workflow:"
    echo "  1. Define behavior in .feature (what it should do)"
    echo "  2. Define data in .schema (what data it uses)"
    echo "  3. Define states in .sm (how it transitions)"
    echo "  4. Regenerate: make regen"
    echo "  5. Verify: make verify"
    echo "  6. Build: make (APE via cosmocc; make CC=cc for native)"
    echo "  7. Test: make test"
    echo ""
}

# ── Main ─────────────────────────────────────────────────────────────────────

case "${1:-help}" in
    new)     cmd_new "$2" "$3" ;;
    regen)   cmd_regen ;;
    verify)  cmd_verify ;;
    build)   cmd_build "$2" ;;
    full)    cmd_full ;;
    help|*)  cmd_help ;;
esac
