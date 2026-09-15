# ══════════════════════════════════════════════════════════════════════════════
# cosmo-bde — BDE with Models
# Behavior Driven Engineering with Models
# ══════════════════════════════════════════════════════════════════════════════
#
# FORMAT-DRIVEN BUILD SYSTEM
#
# This Makefile discovers source formats automatically and builds the dependency
# graph from the format relationships defined in INTEROP_MATRIX.md.
#
# Directory structure encodes format relationships:
#   specs/domain/     → gen/domain/     (schemas, defs)
#   specs/behavior/   → gen/behavior/   (state machines)
#   specs/interface/  → gen/interface/  (APIs, CLIs)
#   specs/parsing/    → gen/parsing/    (lexers, grammars)
#   specs/testing/    → gen/testing/    (BDD features)
#   model/*           → gen/imported/*  (Ring 2 visual tools)
#
# ══════════════════════════════════════════════════════════════════════════════

# ── Toolchain ─────────────────────────────────────────────────────────────────
# For APE builds: make CC=cosmocc (or install cosmocc in PATH)
# For native builds: make CC=cc
CC ?= cc
CFLAGS := -O2 -Wall -Werror -std=c11 -Wno-stringop-truncation

# ── Directories ───────────────────────────────────────────────────────────────
BUILD_DIR := build
TOOLS_DIR := tools
SPECS_DIR := specs
GEN_DIR := gen
SRC_DIR := src
VENDOR_DIR := vendor
MODEL_DIR := model

# ══════════════════════════════════════════════════════════════════════════════
# FORMAT DISCOVERY (the makefile discovers what to build)
# ══════════════════════════════════════════════════════════════════════════════

# Ring 0: Native specs (always processable)
SCHEMAS := $(shell find $(SPECS_DIR) -name "*.schema" 2>/dev/null)
DEFS := $(shell find $(SPECS_DIR) -name "*.def" 2>/dev/null)
SMS := $(shell find $(SPECS_DIR) -name "*.sm" 2>/dev/null)
HSMS := $(shell find $(SPECS_DIR) -name "*.hsm" 2>/dev/null)
LEXERS := $(shell find $(SPECS_DIR) -name "*.lex" 2>/dev/null)
GRAMMARS := $(shell find $(SPECS_DIR) -name "*.y" -o -name "*.grammar" 2>/dev/null)
FEATURES := $(shell find $(SPECS_DIR) -name "*.feature" 2>/dev/null)
RULES := $(shell find $(SPECS_DIR) -name "*.rules" 2>/dev/null)
APIS := $(shell find $(SPECS_DIR) -name "*.api" 2>/dev/null)
GGOS := $(shell find $(SPECS_DIR) -name "*.ggo" 2>/dev/null)
UIS := $(shell find $(SPECS_DIR) -name "*.ui" 2>/dev/null)

# Ring 2: Visual models (processed if tools available)
DRAWIO := $(shell find $(MODEL_DIR) -name "*.drawio" 2>/dev/null)
PROTOS := $(shell find $(SPECS_DIR) $(MODEL_DIR) -name "*.proto" 2>/dev/null)
FBS := $(shell find $(SPECS_DIR) $(MODEL_DIR) -name "*.fbs" 2>/dev/null)
MODELICA := $(shell find $(MODEL_DIR) -name "*.mo" 2>/dev/null)
SIMULINK := $(shell find $(MODEL_DIR) -name "*.slx" 2>/dev/null)
RHAPSODY := $(shell find $(MODEL_DIR) -name "*.emx" 2>/dev/null)
IDLS := $(shell find $(SPECS_DIR) $(MODEL_DIR) -name "*.idl" 2>/dev/null)

# ══════════════════════════════════════════════════════════════════════════════
# OUTPUT MAPPING (format → generated files)
# ══════════════════════════════════════════════════════════════════════════════

# Derive outputs from inputs using naming conventions
schema_to_c = $(patsubst $(SPECS_DIR)/%/%.schema,$(GEN_DIR)/%/%_types.c,$(1))
sm_to_c = $(patsubst $(SPECS_DIR)/%/%.sm,$(GEN_DIR)/%/%_sm.c,$(1))
feature_to_c = $(patsubst $(SPECS_DIR)/%/%.feature,$(GEN_DIR)/%/%_bdd.c,$(1))

# Generated sources (for linking)
GEN_SRCS := $(shell find $(GEN_DIR) -name '*.c' 2>/dev/null)
SRC_SRCS := $(shell find $(SRC_DIR) -name '*.c' 2>/dev/null)
VENDOR_SRCS := $(shell find $(VENDOR_DIR) -name '*.c' 2>/dev/null)

.PHONY: all clean regen verify test tools help app run formats ape ring1 headers lint sanitize tsan e9studio livereload feedback dev

# ══════════════════════════════════════════════════════════════════════════════
# Primary Targets
# ══════════════════════════════════════════════════════════════════════════════

all: tools app
	@echo ""
	@echo "cosmo-bde — BDE with Models"
	@echo "Build complete. Run 'make run' to execute."

help:
	@echo "┌─────────────────────────────────────────────────────────────────────┐"
	@echo "│  cosmo-bde — BDE with Models                                  │"
	@echo "│  Behavior Driven Engineering with Models                            │"
	@echo "├─────────────────────────────────────────────────────────────────────┤"
	@echo "│  make              Build Ring 0 tools + application                 │"
	@echo "│  make regen        Regenerate all (auto-detect tools)               │"
	@echo "│  make verify       Regen + drift check                              │"
	@echo "│  make e9studio     Build live reload tool                           │"
	@echo "│  make feedback     Ring 0→1→2 feedback loop                         │"
	@echo "│  make dev          Watch specs, auto-regen on change                │"
	@echo "│  make formats      Show discovered formats                          │"
	@echo "├─────────────────────────────────────────────────────────────────────┤"
	@echo "│  Ring 0: .schema→types  .def→X-macros  .sm→FSM  .y→parser           │"
	@echo "│  Ring 1: makeheaders, sanitizers, cppcheck                          │"
	@echo "│  Ring 2: StateSmith, protobuf-c (outputs committed)                 │"
	@echo "├─────────────────────────────────────────────────────────────────────┤"
	@echo "│  Workflow: edit spec → make regen → make verify → make → commit     │"
	@echo "│  Live:     make run (T1) → make feedback --app (T2) → edit (T3)     │"
	@echo "└─────────────────────────────────────────────────────────────────────┘"

formats:
	@echo "══════════════════════════════════════════════════════════════════════"
	@echo " DISCOVERED FORMATS"
	@echo "══════════════════════════════════════════════════════════════════════"
	@echo ""
	@echo "── Ring 0: Native Specs ──────────────────────────────────────────────"
	@echo "  .schema:  $(words $(SCHEMAS)) files"
	@[ -z "$(SCHEMAS)" ] || echo "            $(SCHEMAS)"
	@echo "  .def:     $(words $(DEFS)) files"
	@echo "  .sm:      $(words $(SMS)) files"
	@echo "  .hsm:     $(words $(HSMS)) files"
	@echo "  .lex:     $(words $(LEXERS)) files"
	@echo "  .grammar: $(words $(GRAMMARS)) files"
	@echo "  .feature: $(words $(FEATURES)) files"
	@echo "  .rules:   $(words $(RULES)) files"
	@echo "  .api:     $(words $(APIS)) files"
	@echo "  .ggo:     $(words $(GGOS)) files"
	@echo "  .ui:      $(words $(UIS)) files"
	@echo ""
	@echo "── Ring 2: Visual Models ─────────────────────────────────────────────"
	@echo "  .drawio:  $(words $(DRAWIO)) files (StateSmith)"
	@echo "  .proto:   $(words $(PROTOS)) files (protobuf-c)"
	@echo "  .fbs:     $(words $(FBS)) files (flatcc)"
	@echo "  .mo:      $(words $(MODELICA)) files (OpenModelica)"
	@echo "  .slx:     $(words $(SIMULINK)) files (Simulink)"
	@echo "  .emx:     $(words $(RHAPSODY)) files (Rhapsody)"
	@echo "  .idl:     $(words $(IDLS)) files (DDS)"
	@echo ""
	@echo "── Generated ─────────────────────────────────────────────────────────"
	@echo "  gen/*.c:  $(words $(GEN_SRCS)) files"
	@echo ""

# ══════════════════════════════════════════════════════════════════════════════
# Ring 0 Tools (always build these first)
# ══════════════════════════════════════════════════════════════════════════════

RING0_TOOLS := $(BUILD_DIR)/schemagen $(BUILD_DIR)/lemon $(BUILD_DIR)/defgen $(BUILD_DIR)/smgen $(BUILD_DIR)/lexgen $(BUILD_DIR)/bddgen $(BUILD_DIR)/uigen $(BUILD_DIR)/hsmgen $(BUILD_DIR)/apigen $(BUILD_DIR)/implgen $(BUILD_DIR)/sqlgen $(BUILD_DIR)/msmgen $(BUILD_DIR)/siggen $(BUILD_DIR)/clipsgen

tools: $(BUILD_DIR) $(RING0_TOOLS)
	@echo "Ring 0 tools ready"

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/schemagen: $(TOOLS_DIR)/schemagen.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $<

$(BUILD_DIR)/lemon: $(TOOLS_DIR)/lemon.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $<

$(BUILD_DIR)/defgen: $(TOOLS_DIR)/defgen/defgen.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(TOOLS_DIR)/defgen -o $@ $<

$(BUILD_DIR)/smgen: $(TOOLS_DIR)/smgen/smgen.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(TOOLS_DIR)/smgen -o $@ $<

$(BUILD_DIR)/lexgen: $(TOOLS_DIR)/lexgen/lexgen.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(TOOLS_DIR)/lexgen -o $@ $<

$(BUILD_DIR)/bddgen: $(TOOLS_DIR)/bddgen/bddgen.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(TOOLS_DIR)/bddgen -o $@ $<

$(BUILD_DIR)/uigen: $(TOOLS_DIR)/uigen/uigen.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(TOOLS_DIR)/uigen -o $@ $<

$(BUILD_DIR)/hsmgen: $(TOOLS_DIR)/hsmgen/hsmgen.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(TOOLS_DIR)/hsmgen -o $@ $<

$(BUILD_DIR)/apigen: $(TOOLS_DIR)/apigen/apigen.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(TOOLS_DIR)/apigen -o $@ $<

$(BUILD_DIR)/implgen: $(TOOLS_DIR)/implgen/implgen.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(TOOLS_DIR)/implgen -o $@ $<

$(BUILD_DIR)/sqlgen: $(TOOLS_DIR)/sqlgen/sqlgen.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(TOOLS_DIR)/sqlgen -o $@ $<

$(BUILD_DIR)/msmgen: $(TOOLS_DIR)/msmgen/msmgen.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(TOOLS_DIR)/msmgen -o $@ $<

$(BUILD_DIR)/siggen: $(TOOLS_DIR)/siggen/siggen.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(TOOLS_DIR)/siggen -o $@ $<

$(BUILD_DIR)/clipsgen: $(TOOLS_DIR)/clipsgen/clipsgen.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(TOOLS_DIR)/clipsgen -o $@ $<

# ══════════════════════════════════════════════════════════════════════════════
# Ring 1 Tools (optional velocity tools - portable via cosmocc)
# ══════════════════════════════════════════════════════════════════════════════
#
# Ring 1 tools are built as APE binaries for universal portability.
# They work on Linux, macOS, Windows, FreeBSD, OpenBSD, NetBSD (AMD64/ARM64).
#
# See: RING_CLASSIFICATION.md for complete documentation.
#
# ══════════════════════════════════════════════════════════════════════════════

# Ring 1 vendored tools (built from tools/ring1/)
RING1_TOOLS := $(BUILD_DIR)/makeheaders

# Ring 1 compiler: prefer cosmocc for portability, fallback to CC
RING1_CC := $(shell command -v cosmocc >/dev/null 2>&1 && echo "cosmocc" || echo "$(CC)")
RING1_CFLAGS := -O2 -Wall -std=c11 -Wno-unused-variable -Wno-unused-but-set-variable

ring1: $(BUILD_DIR) $(RING1_TOOLS)
	@echo "Ring 1 tools ready (compiler: $(RING1_CC))"
	@if [ "$(RING1_CC)" = "cosmocc" ]; then \
		echo "  APE binaries: portable across Linux/macOS/Windows/BSD"; \
	else \
		echo "  Native binaries: install cosmocc for portable APE builds"; \
	fi

$(BUILD_DIR)/makeheaders: $(TOOLS_DIR)/ring1/makeheaders/makeheaders.c | $(BUILD_DIR)
	$(RING1_CC) $(RING1_CFLAGS) -o $@ $<

# Ring 1: Generate headers from C source
headers: $(BUILD_DIR)/makeheaders
	@echo "Generating headers with makeheaders..."
	@$(BUILD_DIR)/makeheaders $(SRC_SRCS) $(GEN_SRCS) 2>/dev/null || \
		echo "  (no exportable functions found)"

# Ring 1: Static analysis
# Note: cppcheck is not yet available as APE, use system install
lint:
	@if command -v cppcheck >/dev/null 2>&1; then \
		echo "Running cppcheck..."; \
		cppcheck --enable=warning,style --quiet $(SRC_DIR) $(GEN_DIR) 2>&1; \
	else \
		echo "cppcheck not available"; \
		echo "  Linux/macOS: apt/brew install cppcheck"; \
		echo "  Or use: make sanitize (compiler-based checks)"; \
	fi

# Ring 1: Sanitizer builds (compiler built-in, works with cosmocc)
sanitize: CFLAGS += -fsanitize=address,undefined -g
sanitize: clean all
	@echo "Built with AddressSanitizer + UBSan"

tsan: CFLAGS += -fsanitize=thread -g
tsan: clean all
	@echo "Built with ThreadSanitizer"

# ══════════════════════════════════════════════════════════════════════════════
# e9studio (Live Reload / Hot Patching)
# ══════════════════════════════════════════════════════════════════════════════
#
# e9studio provides hot-patching capabilities for APE binaries.
# The livereload tool watches source files and patches running processes.
#
# See: vendors/submodules/e9studio/.claude/CLAUDE.md
#
# ══════════════════════════════════════════════════════════════════════════════

E9STUDIO_DIR := vendors/submodules/e9studio
E9PATCH_DIR := $(E9STUDIO_DIR)/src/e9patch
E9LIVERELOAD_DIR := $(E9STUDIO_DIR)/test/livereload

# e9studio livereload tool (uses unified procmem API)
$(BUILD_DIR)/livereload: $(E9LIVERELOAD_DIR)/livereload.c $(E9PATCH_DIR)/e9procmem.c $(GEN_DIR)/domain/livereload_types.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(E9PATCH_DIR) -I$(GEN_DIR)/domain -o $@ $^

e9studio: $(BUILD_DIR)/livereload
	@echo "e9studio livereload tool built"
	@echo "  Hot-patch running processes without ptrace"
	@echo "  Usage: $(BUILD_DIR)/livereload <PID> [source_file]"

livereload: e9studio
	@echo ""
	@echo "Live Reload Ready"
	@echo "────────────────────────────────────────────────────────────────────"
	@echo "  1. Build and run your app: make run"
	@echo "  2. In another terminal:    $(BUILD_DIR)/livereload \$$(pgrep app) src/main.c"
	@echo "  3. Edit src/main.c and save - changes appear instantly!"
	@echo ""
	@echo "Note: No sudo needed for processes you own (uses process_vm_writev)"
	@echo ""

# Feedback loop: Ring 0→1→2 composability with live reload
feedback:
	@./scripts/feedback-loop.sh

# Dev mode: watch specs and auto-regen
dev:
	@./scripts/feedback-loop.sh --specs

# ══════════════════════════════════════════════════════════════════════════════
# Application (uses generated + handwritten code)
# ══════════════════════════════════════════════════════════════════════════════

app: $(BUILD_DIR)/app
	@echo "Application built"

# Dependencies: main.c + all generated domain types
$(BUILD_DIR)/app: $(SRC_DIR)/main.c $(GEN_DIR)/domain/example_types.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(GEN_DIR)/domain -o $@ $(SRC_DIR)/main.c $(GEN_DIR)/domain/example_types.c

run: app
	@$(BUILD_DIR)/app

# APE build (Actually Portable Executable via cosmocc)
ape:
	@if [ -x "$$(command -v cosmocc 2>/dev/null || echo ~/.cosmocc/bin/cosmocc)" ]; then \
		$(MAKE) clean; \
		CC="$$(command -v cosmocc 2>/dev/null || echo ~/.cosmocc/bin/cosmocc)" $(MAKE) all ring1; \
		echo ""; \
		echo "APE binaries built (portable across Linux/macOS/Windows/BSD)"; \
	else \
		echo "cosmocc not found. Install:"; \
		echo "  mkdir -p ~/.cosmocc"; \
		echo "  curl -L https://cosmo.zip/pub/cosmocc/cosmocc.zip -o /tmp/cosmocc.zip"; \
		echo "  unzip /tmp/cosmocc.zip -d ~/.cosmocc"; \
		exit 1; \
	fi

# ══════════════════════════════════════════════════════════════════════════════
# Regeneration (format-driven, auto-detects tools)
# ══════════════════════════════════════════════════════════════════════════════

regen: tools
	@./scripts/regen-all.sh

verify: tools
	@./scripts/regen-all.sh --verify

# ══════════════════════════════════════════════════════════════════════════════
# Pattern Rules (format → output mapping)
# ══════════════════════════════════════════════════════════════════════════════

# Schema → Types
$(GEN_DIR)/domain/%_types.c $(GEN_DIR)/domain/%_types.h: $(SPECS_DIR)/domain/%.schema $(BUILD_DIR)/schemagen
	@mkdir -p $(GEN_DIR)/domain
	$(BUILD_DIR)/schemagen $< $(GEN_DIR)/domain $*

# Grammar → Parser (Lemon)
$(GEN_DIR)/parsing/%.c $(GEN_DIR)/parsing/%.h: $(SPECS_DIR)/parsing/%.y $(BUILD_DIR)/lemon
	@mkdir -p $(GEN_DIR)/parsing
	$(BUILD_DIR)/lemon $<
	@mv $(SPECS_DIR)/parsing/$*.c $(GEN_DIR)/parsing/ 2>/dev/null || true
	@mv $(SPECS_DIR)/parsing/$*.h $(GEN_DIR)/parsing/ 2>/dev/null || true

# ══════════════════════════════════════════════════════════════════════════════
# Testing
# ══════════════════════════════════════════════════════════════════════════════

test: tools
	@echo "Running BDD tests..."
	@if [ -x "$(BUILD_DIR)/bddgen" ]; then \
		$(BUILD_DIR)/bddgen --run $(SPECS_DIR)/testing/*.feature; \
	else \
		echo "bddgen not built yet, skipping BDD tests"; \
	fi

# ══════════════════════════════════════════════════════════════════════════════
# Cleanup
# ══════════════════════════════════════════════════════════════════════════════

clean:
	rm -rf $(BUILD_DIR)
	@echo "Build artifacts removed (gen/ preserved)"

distclean: clean
	rm -rf $(GEN_DIR)/*
	@echo "Generated code removed"

# ══════════════════════════════════════════════════════════════════════════════
# Documentation / Introspection
# ══════════════════════════════════════════════════════════════════════════════

# Generate dependency graph (requires graphviz)
depgraph:
	@echo "digraph G {"
	@echo "  rankdir=TB;"
	@for f in $(SCHEMAS); do \
		name=$$(basename $$f .schema); \
		echo "  \"$$f\" -> \"gen/domain/$${name}_types.c\";"; \
	done
	@for f in $(SMS); do \
		name=$$(basename $$f .sm); \
		echo "  \"$$f\" -> \"gen/behavior/$${name}_sm.c\";"; \
	done
	@echo "}"

# Show what would be regenerated
whatif:
	@echo "Would regenerate from:"
	@echo "  Schemas:  $(SCHEMAS)"
	@echo "  SMs:      $(SMS)"
	@echo "  Features: $(FEATURES)"
	@echo "  Grammars: $(GRAMMARS)"
