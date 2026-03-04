# cosmo-bde

**Behavior Driven Engineering with Models** — BDE with Models.

Spec-driven C code generation framework for the design and development of any application.
All paths lead to C. All C compiles with cosmocc to Actually Portable Executables.

```
┌─────────────────────────────────────────────────────────────────────────┐
│  BDE with Models!                                              _o_     │
│  Behavior Driven Engineering                                  / | \    │
│  cosmo-bde                                               / \     │
├─────────────────────────────────────────────────────────────────────────┤
│  WORKFLOW                                                               │
│    Edit spec  →  make regen  →  git diff gen/  →  make  →  commit      │
│                                                                         │
│  RINGS                                                                  │
│    0: C + sh + make (always available)                                  │
│    1: Ring 0 + C tools (optional velocity)                              │
│    2: External toolchains (auto-detected, outputs committed)            │
└─────────────────────────────────────────────────────────────────────────┘
```

## Quick Start

```bash
# Clone and build
git clone https://github.com/ludoplex/cosmo-bde.git
cd cosmo-bde
make

# Run the example application
make run

# Edit a spec, regenerate, verify
nano specs/domain/example.schema
make regen
make verify  # Checks for drift
```

## How It Works

1. **Define types** in `specs/domain/example.schema`:
```
type Example {
    id: u64 [doc: "Unique identifier"]
    name: string[64] [doc: "Display name"]
    value: i32 [doc: "Numeric value"]
    enabled: i32 [doc: "Boolean flag (0 or 1)"]
}
```

2. **Generate C code** (Ring 0 tools auto-built, Ring 2 tools auto-detected):
```bash
make regen
```

3. **Use in your code**:
```c
#include "example_types.h"

Example ex;
Example_init(&ex);           // Sets defaults
ex.id = 42;
snprintf(ex.name, sizeof(ex.name), "Hello from specs!");

if (Example_validate(&ex)) {
    // Valid!
}
```

4. **Verify and commit**:
```bash
make verify          # Regen + check for drift
git add -A && git commit -m "Add field to Example"
```

## Architecture Principle

**Separate *authoring* (how you create models) from *shipping* (what's required to build).**

Ring 2 tools (FOSS or commercial) output C code that compiles with cosmocc.
The distinction is licensing/certification, not portability. All tools are auto-detected at regen time.

## Ring Classification

| Ring | Bootstrap | Examples |
|------|-----------|----------|
| **0** | C + sh + make | schemagen, Lemon, SQLite, Nuklear, yyjson, CLIPS |
| **1** | Ring 0 + C tools | gengetopt, cppcheck, sanitizers |
| **2** | External toolchains | StateSmith, protobuf-c, MATLAB, Rhapsody |

**The Rule**: Ring 2 outputs are committed. Builds succeed with just Ring 0.

## Spec Types

| Extension | Generator | Purpose |
|-----------|-----------|---------|
| `.schema` | schemagen | Data types, structs, validation |
| `.def` | defgen | Constants, enums, X-Macros |
| `.sm` | smgen | Flat state machines |
| `.hsm` | hsmgen | Hierarchical state machines |
| `.grammar` | Lemon | Parser grammars (LALR) |
| `.feature` | bddgen | BDD test scenarios |
| `.proto` | protobuf-c | Wire protocol (Ring 2) |
| `.drawio` | StateSmith | Visual state machines (Ring 2) |

See [SPEC_TYPES.md](./SPEC_TYPES.md) for the complete list (15+ spec types).

## Directory Structure

```
cosmo-bde/
├── specs/             # Source of truth (all spec types)
│   ├── domain/        # .schema, .def, .rules
│   ├── behavior/      # .sm, .hsm
│   ├── interface/     # .api, .ggo, .proto
│   ├── parsing/       # .lex, .grammar
│   └── testing/       # .feature
├── model/             # Ring 2 visual sources
│   ├── statesmith/    # .drawio files
│   └── simulink/      # .slx (commercial)
├── gen/               # Generated code (committed, drift-gated)
├── tools/             # Ring 0 generators (schemagen, etc.)
├── vendors/libs/            # Vendored C libraries
├── src/               # Hand-written code
├── scripts/           # Automation (regen-all.sh)
└── .claude/           # LLM context
```

## Commands

```bash
make              # Build Ring 0 tools + application
make regen        # Auto-detect tools, regenerate all code
make verify       # Regen + check for drift (CI gate)
make codesmith-corpus-lock CODESMITH_ZIP="$HOME/Downloads/Generator-85.zip"
make codesmith-corpus CODESMITH_ZIP="$HOME/Downloads/Generator-85.zip"
make codesmith-parity CODESMITH_ZIP="$HOME/Downloads/Generator-85.zip"
make test         # Run BDD tests
make clean        # Remove build artifacts
make help         # Show all targets
```

## Live Reload

Hot-patch running binaries in real-time. Edit C source, save, see changes instantly:

```bash
# Terminal 1: Run your app
./build/app

# Terminal 2: Attach live reload
sudo ./vendors/e9studio/test/livereload/livereload $(pgrep app) src/main.c

# Terminal 3: Edit and save
vim src/main.c
# Watch Terminal 1 - changes appear instantly!
```

Uses stat-based polling (100ms latency). Works on all platforms APE supports.

## Use as GitHub Template

1. Click "Use this template" on GitHub
2. Clone your new repository
3. Run `./scripts/template-init.sh your-project-name`
4. Start editing specs in `specs/domain/`

## Documentation

| Document | Purpose |
|----------|---------|
| [WORKFLOW.md](./WORKFLOW.md) | Full workflow reference |
| [SPEC_TYPES.md](./SPEC_TYPES.md) | All spec types and generators |
| [docs/CODESMITH_PARITY.md](./docs/CODESMITH_PARITY.md) | CodeSmith RE parity harness scaffold |
| [RING_CLASSIFICATION.md](./RING_CLASSIFICATION.md) | Tool ring assignments |
| [STACKS_REFERENCE.md](./STACKS_REFERENCE.md) | Complete vendor tooling |
| [LICENSES.md](./LICENSES.md) | License tracking |
| [.claude/CLAUDE.md](./.claude/CLAUDE.md) | LLM context anchor |

## License

MIT. Individual tools and vendored dependencies retain their original licenses.
See [LICENSES.md](./LICENSES.md).
