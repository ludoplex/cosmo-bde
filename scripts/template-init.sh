#!/bin/sh
# ═══════════════════════════════════════════════════════════════════════════
# template-init.sh - Initialize project after cloning from GitHub template
# ═══════════════════════════════════════════════════════════════════════════
#
# cosmo-bde — BDE with Models
#
# Usage: ./scripts/template-init.sh [project-name]
#
# This script:
#   1. Removes repo-development files (.forge/, repo-ci.yml, CONTRIBUTING.md)
#   2. Verifies Ring 0 tools build
#   3. Runs initial regen to verify everything works
#   4. Prepares for first commit
#
# Requirements: C compiler, sh, make
# ═══════════════════════════════════════════════════════════════════════════

set -e

# ── Configuration ──────────────────────────────────────────────────────────

PROJECT="${1:-$(basename "$(pwd)")}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

# Colors (if terminal supports)
if [ -t 1 ]; then
    RED='\033[0;31m'
    GREEN='\033[0;32m'
    YELLOW='\033[1;33m'
    CYAN='\033[0;36m'
    NC='\033[0m'
else
    RED='' GREEN='' YELLOW='' CYAN='' NC=''
fi

# ── Functions ──────────────────────────────────────────────────────────────

log_info()  { printf "${GREEN}[OK]${NC}    %s\n" "$1"; }
log_warn()  { printf "${YELLOW}[WARN]${NC}  %s\n" "$1"; }
log_error() { printf "${RED}[ERROR]${NC} %s\n" "$1"; }
log_step()  { printf "${CYAN}[STEP]${NC}  %s\n" "$1"; }

check_tool() {
    if command -v "$1" >/dev/null 2>&1; then
        return 0
    else
        return 1
    fi
}

# ── Main Flow ──────────────────────────────────────────────────────────────

echo "═══════════════════════════════════════════════════════════════════════"
echo " cosmo-bde — BDE with Models"
echo " Template Initialization"
echo "═══════════════════════════════════════════════════════════════════════"
echo
echo " Project Name: $PROJECT"
echo
echo "═══════════════════════════════════════════════════════════════════════"

cd "$ROOT_DIR"

# Step 1: Clean up repo-development files (not needed by template users)
log_step "Cleaning up repo-development files..."

CLEANUP_FILES=".forge .forge-cache CONTRIBUTING.md .github/workflows/repo-ci.yml"
CLEANUP_DIRS="strict-purist foss-visual commercial upstream templates examples"

for item in $CLEANUP_FILES; do
    if [ -e "$item" ]; then
        rm -rf "$item"
        log_info "Removed: $item"
    fi
done

for item in $CLEANUP_DIRS; do
    if [ -d "$item" ]; then
        rm -rf "$item"
        log_info "Removed: $item/ (legacy)"
    fi
done

# Rename repo-ci to just ci if template-ci exists
if [ -f ".github/workflows/template-ci.yml" ]; then
    mv .github/workflows/template-ci.yml .github/workflows/ci.yml 2>/dev/null || true
    rm -f .github/workflows/repo-ci.yml 2>/dev/null || true
    log_info "Configured CI workflow"
fi

echo

# Step 2: Verify toolchain
log_step "Verifying toolchain (Ring 0 bootstrap)..."
if check_tool cc || check_tool gcc || check_tool clang; then
    log_info "C compiler found"
else
    log_error "No C compiler found. Install gcc or clang."
    exit 1
fi

if check_tool make; then
    log_info "make found"
else
    log_error "make not found. Install make."
    exit 1
fi

# Step 3: Build Ring 0 tools
log_step "Building Ring 0 generators..."
if make tools 2>&1 | head -5; then
    log_info "Ring 0 tools built successfully"
else
    log_error "Failed to build Ring 0 tools"
    exit 1
fi

# Step 4: Run regen to verify workflow
log_step "Running regen to verify workflow..."
if make regen 2>&1 | tail -10; then
    log_info "Regeneration completed"
else
    log_warn "Regeneration had issues (may be expected on fresh clone)"
fi

# Step 5: Build application
log_step "Building application..."
if make app 2>&1 | head -5; then
    log_info "Application built successfully"
else
    log_error "Failed to build application"
    exit 1
fi

# Step 6: Test run
log_step "Testing application..."
if ./build/app 2>&1 | head -5; then
    log_info "Application runs correctly"
else
    log_warn "Application test had issues"
fi

# Step 7: Check Ring 2 tools
log_step "Checking Ring 2 tools (optional, auto-detected)..."

echo "  Ring 2 FOSS tools:"
check_tool dotnet     && echo "    - StateSmith (.NET): available" || echo "    - StateSmith (.NET): not installed"
check_tool protoc     && echo "    - protobuf-c: available" || echo "    - protobuf-c: not installed"
check_tool flatcc     && echo "    - flatcc: available" || echo "    - flatcc: not installed"
check_tool omc        && echo "    - OpenModelica: available" || echo "    - OpenModelica: not installed"

echo "  Ring 2 Commercial tools:"
check_tool matlab     && echo "    - MATLAB/Simulink: available" || echo "    - MATLAB/Simulink: not installed"
check_tool rhapsodycl && echo "    - IBM Rhapsody: available" || echo "    - IBM Rhapsody: not installed"

echo
log_info "Ring 2 tools are optional. Missing tools' outputs must be pre-committed."

# Step 8: Check cosmocc for APE builds
log_step "Checking cosmocc (APE builds)..."
if check_tool cosmocc; then
    log_info "cosmocc available - APE builds enabled"
    echo "  Build APE binary: CC=cosmocc make clean all"
else
    log_warn "cosmocc not found - the default build needs it"
    echo "  Install the pinned toolchain: make toolchain   (or build natively: make CC=cc)"
fi

# Summary
echo
echo "═══════════════════════════════════════════════════════════════════════"
echo " Initialization Complete"
echo "═══════════════════════════════════════════════════════════════════════"
echo
echo " Your project is ready! Next steps:"
echo
echo "   1. Edit specs:     nano specs/domain/example.schema"
echo "   2. Regenerate:     make regen"
echo "   3. Verify drift:   make verify"
echo "   4. Build:          make"
echo "   5. Run:            make run"
echo
echo " Workflow:"
echo "   Edit spec → make regen → git diff gen/ → make → commit"
echo
echo " Documentation:"
echo "   - README.md              Quick start"
echo "   - WORKFLOW.md            Full workflow reference"
echo "   - SPEC_TYPES.md          All spec types"
echo "   - .claude/CLAUDE.md      LLM context"
echo
echo "═══════════════════════════════════════════════════════════════════════"
