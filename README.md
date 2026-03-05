# cosmo-bde

**Behavior Driven Engineering with Models** — BDE with Models.

Spec-driven C code generation framework for the design and development of any application.
All paths lead to C. All C compiles with cosmocc to Actually Portable Executables.

```
┌─────────────────────────────────────────────────────────────────────────
│  BDE with Models!                                              _o_     
│  Behavior Driven Engineering                                  / | \    
│  cosmo-bde                                               / \     
                       ,ad8 88888888888 888ba,
                     ad88888888888888888888888a,
                   a88888\"8888888888888888888888,
                ,8888"\ \ \"P88888888888888888888b,
                d88 \ \       `""P88888888888888888,
              ,8888b               ""88888888888888,
              d8P'''  ,aa,              ""888888888b
              888bbdd888888ba,  ,I         "88888888,
              8888888888888888ba8"         ,88888888b
             ,888888888888888888b,        ,8888888888
             (88888888888888888888,      ,88888888888,
             d888888888888888888888,    ,8   "8888888b
             88888888888888888888888  .;8'"""  (888888
             8888888888888I"8888888P ,8" ,aaa,  888888
             888888888888I:8888888" ,8"  'b8d'  (88888
             (8888888888I'888888P' ,8) \         88888
              88888888I"  8888P'  ,8")  \        88888
              8888888I'   888"   ,8" (   )       88888
              (8888I"     "88,  ,8"             ,8888P
               888I'       "P8 ,8"   ____      ,88888)
              (88I'          ",8"  M""""""M   ,888888'
             ,8I"            ,8(    "aaaa"   ,8888888
         ,8I'             ,888a           ,8888888)
       ,8I'            ,888888,       ,888888888
    ,8I'             ,8888888'`-===-'888888888'
 ,8I'            ,8888888"        88888888"
 8I'            ,8"    88         "888888P
8I            ,8'     88          `P888"
8I           ,8I      88            "8ba,.
(8,         ,8P'      88              88""8bma,.
 8I        ,8P'       88,              "8b   ""P8ma,
 (8,      ,8d"        `88,               "8b     `"8a
  8I     ,8dP         ,8X8,                "8b.    :8b
  (8    ,8dP'  ,I    ,8XXX8,                `88,    8)
   8,   8dP'  ,I    ,8XxxxX8,     I,         8X8,  ,8
   8I   8P'  ,I    ,8XxxxxxX8,     I,        `8X88,I8
   I8,  "   ,I    ,8XxxxxxxxX8b,    I,        8XXX88I,
   `8I      I'  ,8XxxxxxxxxxxxXX8    I        8XXxxXX8,
    8I     (8  ,8XxxxxxxxxxxxxxxX8   I        8XxxxxxXX8,
   ,8I     I[ ,8XxxxxxxxxxxxxxxxxX8  8        8XxxxxxxxX8,
   d8I,    I[ 8XxxxxxxxxxxxxxxxxxX8b 8       (8XxxxxxxxxX8,
   888I    `8,8XxxxxxxxxxxxxxxxxxxX8 8,     ,8XxxxxxxxxxxX8
   8888,    "88XxxxxxxxxxxxxxxxxxxX8)8I    .8XxxxxxxxxxxxX8
  ,8888I     88XxxxxxxxxxxxxxxxxxxX8 `8,  ,8XxxxxxxxxxxxX8"
  d88888     `8XXxxxxxxxxxxxxxxxxX8'  `8,,8XxxxxxxxxxxxX8"
  888888I     `8XXxxxxxxxxxxxxxxX8'    "88XxxxxxxxxxxxX8"
  88888888bbaaaa88XXxxxxxxxxxxXX8)      )8XXxxxxxxxxXX8"
  8888888I, ``""""""8888888888888888aaaaa8888XxxxxXX8"
  (8888888I,                      .  ```"""""88888P"
   88888888I,                   ,8I   8,       I8"
    """88888I,                ,8I'    "I8,    ;8"
           `8I,             ,8I'       `I8,   8)
            `8I,           ,8I'          I8  :8'
             `8I,         ,8I'           I8  :8
              `8I       ,8I'             `8  (8
               8I     ,8I'                8  (8;
               8I    ,8"                  I   88,
              .8I   ,8'                       8"8,
              (PI   '8                       ,8,`8,
             .88'            ,@           .a8X8,`8,
             (88             @@         ,a8XX888,`8,
            (888             @'       ,d8XX8"  "b `8,
           .8888,                     a8XXX8"    "a `8,
          .888X88                   ,d8XX8I"      9, `8,
         .88:8XX8,                 a8XxX8I'       `8  `8,
        .88' 8XxX8a             ,ad8XxX8I'        ,8   `8,
        d8'  8XxxxX8ba,      ,ad8XxxX8I"          8  ,  `8,
       (8I   8XxxxxxX888888888XxxxX8I"            8  II  `8
       8I'   "8XxxxxxxxxxxxxxxxxxX8I'            (8  8)   8;
      (8I     8XxxxxxxxxxxxxxxxxX8"              (8  8)   8I
      8P'     (8XxxxxxxxxxxxxxX8I'                8, (8   :8
     (8'       8XxxxxxxxxxxxxxX8'                 `8, 8    8
     8I        `8XxxxxxxxxxxxX8'                   `8,8   ;8
     8'         `8XxxxxxxxxxX8'                     `8I  ,8'
     8           `8XxxxxxxxX8'                       8' ,8'
     8            `8XxxxxxX8'                        8 ,8'
     8             `8XxxxX8'                        d' 8'
     8              `8XxxX8                         8 8'
     8                "8X8'                         "8"
     8,                `88                           8
     8I                ,8'                          d)
     `8,               d8                          ,8
      (b               8'                         ,8'
       8,             dP                         ,8'
       (b             8'                        ,8'
        8,           d8                        ,8'
        (b           8'                       ,8'
         8,         a8                       ,8'
         (b         8'                      ,8'
          8,       ,8                      ,8'
          (b       8'                     ,8'
           8,     ,8                     ,8'
           (b     8'                    ,8'
            8,   d8                    ,8'
            (b  ,8'                   ,8'
             8,,I8                   ,8'
             I8I8'                  ,8'
             `I8I                  ,8'
              I8'                 ,8'
              "8                 ,8'
              (8                ,8'
              8I               ,8'
              (b,   8,        ,8)
              `8I   "88      ,8i8,
               (b,          ,8"8")
               `8I  ,8      8) 8 8
                8I  8I      "  8 8
                (b  8I         8 8
                `8  (8,        b 8,
                 8   8)        "b"8,
                 8   8(         "b"8
                 8   "I          "b8,
                 8                `8)
                 8                 I8
                 8                 (8
                 8,                 8,
                 Ib                 8)
                 (8                 I8
                  8                 I8
                  8                 I8
                  8,                I8
                  Ib                8I
                  (8               (8'
                   8               I8
                   8,              8I
                   Ib             (8'
                   (8             I8
                   `8             8I
                    8            (8'
                    8,           I8
                    Ib           8I
                    (8           8'
                     8,         (8
                     Ib         I8
                     (8         8I
                     (8         8I
                      8,        8'
                      (b       (8
                       8,      I8
                       I8      I8
                       (8      I8
                        8      I8,
                        8      8 8,
                        8,     8 8'
                       ,I8     "8"
                      ,8"8,     "b
                     ,8' `8      8
                    ,8'   8      8,
                   ,8'    (a     "b
                  ,8'     `8      (b
                  I8/      8       8,
                  I8-/     8       `8,
                  (8/-/    8        `8,
                   8I/-/  ,8         `8
                   `8I/--,I8        \-8)
                    `8I,,d8I       \-\8)
                      "bdI"8,     \-\I8
                           `8,   \-\I8'
                            `8,,--\I8'
                             `Ib,,I8'
                              `I8I'








├─────────────────────────────────────────────────────────────────────────┤
│  WORKFLOW                                                               │
│    Edit spec  →  make regen  →  git diff gen/  →  make  →  commit       │
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
| [RING_CLASSIFICATION.md](./RING_CLASSIFICATION.md) | Tool ring assignments |
| [STACKS_REFERENCE.md](./STACKS_REFERENCE.md) | Complete vendor tooling |
| [LICENSES.md](./LICENSES.md) | License tracking |
| [.claude/CLAUDE.md](./.claude/CLAUDE.md) | LLM context anchor |

## License

MIT. Individual tools and vendored dependencies retain their original licenses.
See [LICENSES.md](./LICENSES.md).
