# MY_INSTRUCTIONS.md — LLM Agent Context

**For:** Claude, Copilot, or any LLM continuing work on cosmo-bde
**Repo:** https://github.com/ludoplex/cosmo-bde
**Local:** `/home/user/workspace/bde-with-models/`

---

## What This Project Is

**cosmo-bde** = Cosmopolitan + Behavior Driven Engineering

A spec-driven code generation framework where:
- All tools compile with **cosmocc** to APE (Actually Portable Executable)
- Ring 0 generators are pure C11, self-hosted via X-macros
- Ring 2 tools (external) have outputs committed, builds work with Ring 0 only

**The methodology is "BDE with Models"** — LLM agents (like you) are the "models" in the engineering process.

---

## Critical Rules

### DO NOT
- Use TinyCC (produces relocation errors with cosmopolitan.a)
- Create `vendor/`, `upstream/`, `third-party/` directories (use `vendors/`)
- Modify directory structure (it's immutable — see CLAUDE.md)
- Skip reading `.claude/CLAUDE.md` before making changes

### ALWAYS
- Follow X-macro dogfooding pattern: `{tool}_tokens.def` + `{tool}_self.h`
- Use section separators: `/* ── Section Name ─────────────── */`
- Test with `make` before committing
- Update SPEC_TYPES.md when adding generators

---

## Directory Structure (IMMUTABLE)

```
cosmo-bde/
├── vendors/
│   ├── libs/           # Single-file libraries (yyjson); SQLite = cosmopolitan third_party/sqlite3
│   └── submodules/     # Git submodules (e9studio, cosmo-sokol, etc.)
├── tools/              # Ring 0 generators (pure C)
│   ├── {name}/
│   │   ├── {name}.c
│   │   ├── {name}_self.h
│   │   └── {name}_tokens.def
│   └── ring1/          # Velocity tools (makeheaders, gengetopt)
├── specs/              # Human-authored specifications
│   └── {layer}/*.{format}
├── gen/                # Generated code (committed)
│   └── {layer}/
├── src/                # Application source
└── scripts/            # Automation
```

---

## Ring 0 Generator Pattern

Every generator follows this structure:

```c
// tools/foogen/foogen.c
#include "foogen_self.h"  // X-macro expansion

// tools/foogen/foogen_tokens.def
TOK(KEYWORD, "keyword", keyword, "Description")
TOK(IDENT,   "...",     ident,   "Identifier")

// tools/foogen/foogen_self.h
typedef enum {
    FOOGEN_TOK_EOF = 0,
#define TOK(name, lex, kind, doc) FOOGEN_TOK_##name,
#include "foogen_tokens.def"
#undef TOK
} foogen_token_t;
```

---

## Current Status (as of 2026-03-01)

| Component | Status |
|-----------|--------|
| `make` | ✓ Works |
| `make regen` | ✓ Works |
| `make test` | ✗ Broken (bddgen --run missing) |
| schemagen, defgen, smgen, lexgen, uigen | ✓ Working |
| bddgen, hsmgen, apigen | ✓ Working (new) |
| implgen, msmgen, clipsgen, sqlgen, siggen | ✗ Not implemented |

---

## Key Commands

```bash
make              # Build all tools + app
make tools        # Build Ring 0 tools only
make regen        # Regenerate from specs
make verify       # Regen + drift check
make test         # Run tests (currently broken)
make formats      # Show discovered formats
make run          # Execute built app
```

---

## Files to Read First

1. `.claude/CLAUDE.md` — Primary context (always read first)
2. `SPEC_TYPES.md` — Generator status matrix
3. `MY_PLAN.md` — Remaining work to 100%
4. `INTEROP_MATRIX.md` — Format relationships

---

## Naming Conventions

| Thing | Pattern | Example |
|-------|---------|---------|
| Generator | `{name}gen` | `schemagen`, `bddgen` |
| Token file | `{name}_tokens.def` | `bddgen_tokens.def` |
| Self-host header | `{name}_self.h` | `bddgen_self.h` |
| Generated types | `{prefix}_types.c,h` | `example_types.c` |
| Generated SM | `{prefix}_sm.c,h` | `traffic_sm.c` |
| Generated HSM | `{prefix}_hsm.c,h` | `trafficlight_hsm.c` |
| Generated BDD | `{prefix}_bdd.c,h` + `{prefix}_steps.c` | `e9livereload_bdd.c` |

---

## Submodules (vendors/submodules/)

| Submodule | Purpose | Ring |
|-----------|---------|------|
| e9studio | Live reload, binary patching | 0 |
| cosmo-sokol | GUI (Sokol + Nuklear) | 0 |
| ludoplex-binaryen | WASM IR diffing | 2 |
| StateSmith | State machines from .drawio | 2 |
| eez-studio | Embedded GUI | 2 |
| lvgl | Graphics library | 2 |
| protobuf-c | Serialization | 2 |
| OpenModelica | Simulation | 2 |

---

## When Resuming Work

1. Read this file and `MY_PLAN.md`
2. Check `make` still works
3. Pick next task from MY_PLAN.md
4. Follow the pattern of existing generators
5. Update SPEC_TYPES.md when done
6. Commit with descriptive message + Co-Authored-By

---

## Contact / Issues

- Repo: https://github.com/ludoplex/cosmo-bde
- Issues: https://github.com/ludoplex/cosmo-bde/issues
