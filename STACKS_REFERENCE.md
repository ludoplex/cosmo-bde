# cosmo-bde Stacks Reference

> **Behavior Driven Engineering with Models** — BDE with Models.
>
> Framework for the design and development of any application.
> Complete inventory of all tools and approaches.

---

## All Paths Lead to C

**Key Insight:** All Ring 2 tools—FOSS or commercial—generate C source code that compiles with `cosmocc`. There's no "portable vs commercial output" distinction. The regen script auto-detects available tools and uses them. Missing tools are skipped; their outputs are already committed.

| Tool Type | Examples | Output |
|-----------|----------|--------|
| **FOSS** | StateSmith, EEZ, protobuf-c, OpenModelica | Zero-dependency C |
| **Commercial** | Rhapsody, Simulink, Qt, RTI DDS | C (may need runtime) |

Both produce C. Both compile with cosmocc. The distinction is licensing and certification, not portability.

---

## Ring 0: Bootstrap Layer (C+sh+make only)

**Philosophy:** "The Output is C, even if the Tool isn't."
Ring 0 tools build from clean checkout with only `sh`, `make`, and a C compiler.

| Layer | Tool | What it Generates |
|-------|------|-------------------|
| **Meta-Model** | schemagen (in-tree C) | Structs, validation, serializers from `.schema` |
| **Constants** | defgen (in-tree C) | Enums, X-Macros, config from `.def` |
| **State Machine** | smgen (in-tree C) | Flat FSM code from `.sm` |
| **Parser** | Lemon (public domain) | Thread-safe LALR parsers from `.grammar` |
| **Lexer** | lexgen (in-tree C) | Table-driven lexer from `.lex` |
| **BDD Tests** | bddgen (in-tree C) | C test harness from `.feature` |
| **UI** | Nuklear (header-only) | Immediate-mode GUI |
| **Runtime** | Cosmopolitan/cosmocc | Single APE binary for all platforms |

---

## Ring 2: Visual Authoring Tools (Auto-Detected)

All Ring 2 tools generate C code. The regen script auto-detects which are installed.

### FOSS Tools (Free)

| Layer | Tool | What it Generates |
|-------|------|-------------------|
| **State Machine** | StateSmith (.NET) | Human-readable C FSM from `.drawio` |
| **Physics/Math** | OpenModelica (C++/OCaml) | C simulation code from `.mo` |
| **UI/HMI** | EEZ Studio (Node.js) | C/C++ GUI code for LVGL |
| **Serialization** | protobuf-c (C++) | C structs + wire format from `.proto` |
| **Zero-Copy** | flatcc (C) | C structs for FlatBuffers from `.fbs` |
| **WASM** | Binaryen (C++) | Optimized WASM modules |

### Commercial Tools (Licensed)

| Layer | Tool | What it Generates |
|-------|------|-------------------|
| **Architecture** | IBM Rhapsody | C/C++ skeleton, threading, statecharts |
| **Logic/Math** | Simulink + Stateflow | C logic (state machines, DSP, control) |
| **Code Engine** | Embedded Coder | DO-178C traceable ANSI C |
| **UI/HMI** | Qt Design Studio | QML/C++ bridge (needs Qt runtime) |
| **Data/Comms** | RTI Connext DDS | C pub/sub (needs DDS runtime) |

**Key Insight:** Ring 2 outputs are committed. Build succeeds with just C+sh+make.

**Unified Workflow:**
1. Edit specs (`.schema`, `.def`, `.sm`, etc.) or visual models (`.drawio`, `.slx`)
2. `make regen` → Auto-detects tools, generates C code
3. `git diff --exit-code gen/` → Drift gate
4. `make` → cosmocc produces `app.com` (APE binary)

---

## Tool Comparison

| Feature | FOSS Tools | Commercial Tools |
|---------|------------|------------------|
| **Examples** | StateSmith, EEZ, protobuf-c | Rhapsody, Simulink, Qt |
| **License** | Free | $10K-$100K+/seat |
| **Certification** | Manual | DO-178C, ISO 26262, ASPICE |
| **Traceability** | Manual | Automated |
| **Output** | Zero-dependency C | C (some need runtime) |
| **APE Compatible** | Yes | Yes (generated C) |

---

## The C+sh Minimalist Full-Stack (Portable Profile Foundation)

### Design Principle

Every component is either **written in C, invoked from sh, or a C library you `#include`**.
This is the foundation of the Portable profile—Ring 0 can build without Ring 2 tools.

Properties that enable "web dev ease":
1. **Copy a `.h` file → use it** (no package manager)
2. **One binary deploys everywhere** (no Docker, no VM)
3. **Edit → `make` → run in under 2 seconds**

### Complete Tool Inventory

```
┌─────────────────────────────────────────────────────────────┐
│                    YOU WRITE                                 │
│              C source    +    sh scripts                     │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│  ┌─── SCAFFOLDING & CODEGEN ────────────────────────────┐   │
│  │  C preprocessor (X-macros, _Generic)                 │   │
│  │  M4 (POSIX macro processor)                          │   │
│  │  sh heredocs + sed/awk (simple generation)           │   │
│  │  makeheaders (auto .h from .c)                       │   │
│  │  xxd -i / incbin.h (embed assets)                    │   │
│  └──────────────────────────────────────────────────────┘   │
│                                                              │
│  ┌─── PARSING (when needed) ────────────────────────────┐   │
│  │  re2c       (regex → C lexer; input is C)            │   │
│  │  lemon      (BNF → C parser; from SQLite)            │   │
│  │  gperf      (word list → C perfect hash)             │   │
│  └──────────────────────────────────────────────────────┘   │
│                                                              │
│  ┌─── APPLICATION FRAMEWORK ────────────────────────────┐   │
│  │  sokol_app.h    → window, lifecycle, input           │   │
│  │  sokol_gfx.h    → GPU graphics                       │   │
│  │  sokol_audio.h  → audio output                       │   │
│  │  sokol_fetch.h  → async file/network loading         │   │
│  │  sokol_time.h   → high-res timing                    │   │
│  │  sokol_log.h    → logging callback                   │   │
│  │  sokol_glue.h   → ties app+gfx together              │   │
│  │  redbean        → HTTP server + Lua + SQLite         │   │
│  └──────────────────────────────────────────────────────┘   │
│                                                              │
│  ┌─── UI ───────────────────────────────────────────────┐   │
│  │  nuklear.h      → full immediate-mode GUI            │   │
│  │  microui.h      → minimal IMGUI (~1100 LOC)          │   │
│  │  clay.h         → layout engine                      │   │
│  │  (or HTML/CSS via redbean for web UI)                │   │
│  └──────────────────────────────────────────────────────┘   │
│                                                              │
│  ┌─── DATA ─────────────────────────────────────────────┐   │
│  │  sqlite3.c/.h   → full SQL database (1 file)         │   │
│  │  yyjson.c/.h    → fast JSON parse/emit               │   │
│  │  stb_ds.h       → typed dynamic arrays + maps        │   │
│  │  uthash.h       → intrusive hash tables              │   │
│  │  ini.h          → INI config files                   │   │
│  └──────────────────────────────────────────────────────┘   │
│                                                              │
│  ┌─── GRAPHICS / MEDIA ─────────────────────────────────┐   │
│  │  stb_image.h         → load PNG/JPG/BMP/GIF          │   │
│  │  stb_image_write.h   → save PNG/JPG/BMP              │   │
│  │  stb_truetype.h      → TrueType font raster          │   │
│  │  stb_rect_pack.h     → rectangle bin packing         │   │
│  │  HandmadeMath.h      → vec/mat/quat (SIMD)           │   │
│  │  miniaudio.h         → cross-platform audio          │   │
│  │  olivec.h            → software renderer             │   │
│  │  cute_*.h            → physics, collision, etc       │   │
│  └──────────────────────────────────────────────────────┘   │
│                                                              │
│  ┌─── STRINGS / TEXT ───────────────────────────────────┐   │
│  │  stb_sprintf.h  → fast printf replacement            │   │
│  │  utf8.h (sheredom) → UTF-8 string ops                │   │
│  │  sds.h (antirez)  → simple dynamic strings           │   │
│  └──────────────────────────────────────────────────────┘   │
│                                                              │
│  ┌─── NETWORKING (beyond redbean) ──────────────────────┐   │
│  │  mongoose.c/.h  → embedded HTTP/WebSocket/MQTT       │   │
│  │  (or raw cosmopolitan sockets)                       │   │
│  └──────────────────────────────────────────────────────┘   │
│                                                              │
│  ┌─── OPTIONAL EMBEDDED SCRIPTING ──────────────────────┐   │
│  │  Lua 5.4        → 1-file amalgamation, 250KB         │   │
│  │  s7 (Scheme)    → 1 .c file, trivial embed           │   │
│  └──────────────────────────────────────────────────────┘   │
│                                                              │
│  ┌─── TESTING ──────────────────────────────────────────┐   │
│  │  greatest.h     → single-header test framework       │   │
│  │  munit.h        → single-header with stats           │   │
│  │  assert.h       → standard C                         │   │
│  │  sanitizers     → -fsanitize=address,undefined       │   │
│  │  AFL++          → coverage-guided fuzzing            │   │
│  │  gcov + lcov    → coverage measurement               │   │
│  └──────────────────────────────────────────────────────┘   │
│                                                              │
│  ┌─── BUILD ────────────────────────────────────────────┐   │
│  │  Make           → dependency-driven rebuild          │   │
│  │  sh scripts     → orchestration, scaffolding         │   │
│  │  entr           → watch files, rebuild on save       │   │
│  │  ccache         → compilation caching                │   │
│  └──────────────────────────────────────────────────────┘   │
│                                                              │
├─────────────────────────────────────────────────────────────┤
│  COMPILER + RUNTIME                                          │
│  GCC or Clang (with cosmopolitan toolchain)                  │
│  Cosmopolitan libc (APE binary format)                       │
│  GDB / Valgrind (debugging)                                  │
├─────────────────────────────────────────────────────────────┤
│  DEPLOYMENT                                                  │
│  Single APE binary + optional ZIP-embedded assets            │
│  Runs on Linux, macOS, Windows, FreeBSD, OpenBSD, NetBSD     │
│  No installer. No runtime. No dependencies. Copy & run.      │
└─────────────────────────────────────────────────────────────┘
```

---

## What Replaces What

| Heavy Tool (non-C) | C+sh Replacement | What You Write |
|--------------------|------------------|----------------|
| **MetaEdit+** (Java) | C preprocessor X-macros + M4 | C macros, M4 templates |
| **mbeddr** (JetBrains MPS) | State machines as C data tables | `{STATE_IDLE, EVENT_CLICK, STATE_ACTIVE, action_fn}` |
| **GNU AutoGen** (Guile) | M4 + sh heredocs + sed/awk | `m4 -DNAME=foo template.m4 > foo.c` |
| **protobuf-c** (.proto) | Hand-written C or yyjson | C structs + `fwrite`/`fread` |
| **CMock** (Ruby) | FFF.h (C macro-only fakes) | `FAKE_VALUE_FUNC(int, my_func, int)` |
| **Meson** (Python) | Make + sh | `Makefile` + `build.sh` |
| **Coccinelle** (OCaml) | sed/awk for text transforms | `sed -i 's/old_api/new_api/g' *.c` |
| **Frama-C** (OCaml) | assert + sanitizers + fuzzing | `-fsanitize=address,undefined` |
| **npm ecosystem** | Single-header libs in `lib/` | `cp nuklear.h lib/` |
| **Docker** | Cosmopolitan fat binary | One binary. `scp`. Run. |
| **React / Vue** | nuklear.h or microui.h | `nk_begin(); nk_button(); nk_end();` |

---

## The 12-Tool "Strict C" Stack

Tools that are purely C or sh, no C++/Python/Ruby dependencies:

| # | Tool | Purpose | Written In | Input | Output |
|---|------|---------|------------|-------|--------|
| 1 | C Preprocessor | Schema & Meta-model | C | `.def` (X-Macros) | Structs, SQL, UI Glue |
| 2 | Lemon | Parsing | C | `.y` (Grammar) | `parser.c` |
| 3 | GenGetOpt | CLI Args | C | `.ggo` | `cmdline.c` |
| 4 | FlatCC | Serialization | C | `.fbs` | `_builder.h`, `_reader.h` |
| 5 | XXD | Asset Embedding | C | Binary | C Byte Array |
| 6 | Makeheaders | Header Gen | C | `.c` (Handwritten) | `.h` (Interface) |
| 7 | Unifdef | Pre-process cleanup | C | C Source | Cleaned C Source |
| 8 | Awk | Doc Generation | C/sh | `.def` + `.c` | `.md` (Documentation) |
| 9 | Make | Build System | C | `Makefile` | Binary |
| 10 | Sh | Orchestration | C | Shell scripts | (Process control) |
| 11 | CC / CosmoCC | Compiler | C | `.c` | Binary |
| 12 | Git | Enforcement | C | Repo state | Diff / Exit Code |

---

## Ring Classification System

### Ring 0: Bootstrap Layer (C + sh + make)

**Requirement:** Must build from clean checkout with only `sh`, `make`, and a C compiler.

#### In-Tree Generators
| Tool | Purpose | Source |
|------|---------|--------|
| schemagen.c | Generate structs, serializers, validators | Local |
| lexgen.c | Generate table-driven lexer | Local |
| bin2c.c | Embed binary resources as C arrays | Local |
| smgen.c | Generate FSMs from `.sm` | Local |
| defgen.c | Generate constants, enums, X-Macros | Local |
| bddgen.c | Generate test harness from `.feature` | Local |

#### Vendored Libraries
| Library | License | Purpose |
|---------|---------|---------|
| SQLite | Public Domain | Embedded database |
| Lemon | Public Domain | LALR(1) parser generator |
| CivetWeb | MIT | Embedded HTTP server |
| Nuklear | MIT/PD | Immediate-mode GUI |
| yyjson | MIT | Fast JSON parser |
| CLIPS | Public Domain | Rules/expert system engine |
| kilo | BSD-2 | Minimal text editor |
| e9patch | GPL-3.0 | Binary patching |

### Ring 1: Velocity Tools (Ring 0 + C utilities)

| Tool | Purpose | Fallback |
|------|---------|----------|
| gengetopt | CLI argument parser generator | Hand-written parser |
| makeheaders | Auto-generate header files | Manual headers |
| AddressSanitizer | Memory error detection | Valgrind |
| UBSan | Undefined behavior detection | Manual review |
| cppcheck | Static analysis | Manual review |

### Ring 2: Authoring Appliances (External toolchains)

**Rule:** Outputs MUST be committed to repository.

#### State Machine Generators
| Tool | Toolchain | Generated Code | License |
|------|-----------|----------------|---------|
| StateSmith | .NET (C#) | Zero-dependency C | Apache-2.0 |
| IBM Rhapsody | IBM | C/C++ | Commercial |
| Simulink/Stateflow | MATLAB | C/C++ | Commercial |

#### Data/Schema Generators
| Tool | Toolchain | Generated Code | License |
|------|-----------|----------------|---------|
| protobuf-c | C++ compiler | C serialization | BSD-2 |
| rtiddsgen (RTI DDS) | Java | Type support from IDL | Commercial |
| OpenModelica | C++/OCaml | C simulation code | OSMC-PL |

#### UI/Visual Editors
| Tool | Toolchain | Generated Code | License |
|------|-----------|----------------|---------|
| EEZ Studio | Node.js/Electron | C or C++ | GPL-3.0 |
| Qt Design Studio | Qt | QML/C++ | Commercial/GPL |

---

## Licensing Considerations

### Default Choices (Permissive)
| Need | Tool | License | Notes |
|------|------|---------|-------|
| HTTP Server | CivetWeb | MIT | Fork of Mongoose pre-GPL |
| Database | SQLite | Public Domain | Reused from Cosmopolitan third_party/sqlite3 (not vendored) |
| GUI | Nuklear | MIT/PD | Single header |
| JSON | yyjson | MIT | Fast, small |

### Avoid in Default Stack
| Tool | License | Issue |
|------|---------|-------|
| Mongoose (post-2019) | GPLv2/Commercial | GPL contamination |
| libmicrohttpd | LGPLv2.1+ | LGPL constraints |

---

## Two Build Profiles

### Profile A: Portable Source (Development)
- Compiles with standard `cc` (GCC/Clang/MSVC)
- Links against system libraries (Cocoa, GTK, etc.)
- Used for debugging, sanitizers, local tools

### Profile B: APE Distribution (Release)
- Compiles with `cosmocc`
- Statically links everything
- Single `.com` binary for all platforms
- Acknowledges arch-specific loaders (`ape-aarch64.elf`, `ape-x86_64.elf`)

---

## The Enforcement Mechanism ("The Gate")

### Source of Truth: `schema.def`

```c
/* @Table: User
 * @Desc: Represents a registered player in the system.
 */
MODEL_START(User)
    X(int,    id,    "INTEGER", "PRIMARY KEY", "Unique ID")
    X(char*,  name,  "TEXT",    "NOT NULL",    "Display name")
    X(int,    score, "INTEGER", "DEFAULT 0",   "Current score")
MODEL_END()
```

### Generator: `tools/schemagen.c`

Reads schema, emits:
- `gen/model.h` — structs + constants
- `gen/model_sql.h` — SQLite DDL
- `gen/model_stmt.h` — prepared statements
- `gen/model_bind.h` — bind/extract helpers
- `gen/model_json.h` — yyjson encode/decode
- `gen/model_ui.h` — Nuklear widget metadata
- `docs/schema.md` — literate documentation

### Gate 1: Doc Completeness
schemagen fails if any TABLE/FIELD doc string is empty.

### Gate 2: Drift Detection
```makefile
check-drift: regen
    @git diff --exit-code gen/ docs/schema.md || \
    (echo "FAIL: Generated code/docs differ from schema.def." && exit 1)
```

### Pre-commit Hook
```sh
#!/bin/sh
# Ban manual struct definitions
if grep -r "struct User {" src/; then
    echo "ERROR: Do not define structs manually. Add to schema.def."
    exit 1
fi
```

---

## Complete Tool List by Category

### Code Generators (C tools that emit C)

| Tool | Input | Output | Ring |
|------|-------|--------|------|
| schemagen | `.schema`/`.def` | structs, SQL, JSON, UI | 0 |
| defgen | `.def` | constants, enums, X-Macros | 0 |
| smgen | `.sm` | state machines | 0 |
| lexgen | `.lex` | table-driven lexer | 0 |
| bddgen | `.feature` | test harness | 0 |
| Lemon | `.grammar` | LALR parser | 0 |
| gengetopt | `.ggo` | CLI parser | 1 |
| flatcc | `.fbs` | zero-copy serialization | 0/1 |
| xxd / bin2c | binary | C byte array | 0 |
| makeheaders | `.c` | `.h` headers | 1 |

### Runtime Libraries (Single-header or amalgamation)

| Library | Category | LOC | Purpose |
|---------|----------|-----|---------|
| sokol_app.h | Framework | ~8K | Window, input, lifecycle |
| sokol_gfx.h | Framework | ~14K | GPU graphics abstraction |
| sokol_audio.h | Framework | ~2K | Audio output |
| nuklear.h | UI | ~18K | Immediate-mode GUI |
| microui.c | UI | ~1.1K | Tiny IMGUI |
| clay.h | UI | ~2K | Layout engine |
| sqlite3.c | Data | ~238K | Full SQL database |
| yyjson.h | Data | ~10K | Fast JSON |
| stb_ds.h | Data | ~2K | Dynamic arrays + maps |
| uthash.h | Data | ~1K | Hash tables |
| stb_image.h | Media | ~8K | Image loading |
| stb_truetype.h | Media | ~5K | Font rasterization |
| HandmadeMath.h | Math | ~3K | Vector/matrix |
| miniaudio.h | Audio | ~90K | Full audio engine |
| log.c | Utility | ~200 | Logging |
| greatest.h | Testing | ~1K | Unit tests |
| fff.h | Testing | ~1K | Mock functions |

### External Authoring Tools (Ring 2)

| Tool | Category | Input Format | Output |
|------|----------|--------------|--------|
| StateSmith | State Machines | `.drawio` | C FSM |
| EEZ Studio | UI | `.eez-project` | LVGL C |
| Protobuf-c | Serialization | `.proto` | C structs |
| OpenModelica | Simulation | `.mo` | C simulation |
| Binaryen | WASM | `.wasm` | Optimized WASM |
| Rhapsody | Architecture | UML | C/C++ skeleton |
| Simulink | Algorithms | `.slx` | C via Embedded Coder |

---

## Directory Structure

```
cosmo-bde/
├── Makefile                    # The Orchestrator
├── schema/
│   ├── app.schema              # X-macro truth (data + docs + flags)
│   └── tokens.def              # keyword/operator truth
├── specs/                      # All specification files
│   ├── *.schema                # Type definitions
│   ├── *.def                   # Constants, enums
│   ├── *.sm                    # State machines
│   ├── *.lex                   # Lexer tokens
│   ├── *.grammar               # Parser grammars
│   ├── *.feature               # BDD scenarios
│   └── ring1/                  # Ring 1 tool specs
│       ├── gengetopt.ggo
│       └── cppcheck.schema
├── model/                      # Optional: Ring 2 sources
│   ├── *.drawio                # StateSmith diagrams
│   ├── *.proto                 # Protobuf schemas
│   └── *.mo                    # Modelica models
├── tools/                      # Ring 0 C tools
│   ├── schemagen.c
│   ├── defgen.c
│   ├── lexgen.c
│   ├── smgen.c
│   ├── bddgen.c
│   ├── bin2c.c
│   └── lemon.c / lempar.c
├── gen/                        # Generated (never hand-edit)
│   ├── schemagen/
│   ├── defgen/
│   ├── statesmith/             # Ring 2 imports
│   └── protobuf/               # Ring 2 imports
├── vendors/libs/                     # Vendored libraries
│   ├── sqlite/sqlite3.c
│   ├── civetweb/
│   ├── nuklear/
│   ├── sokol/
│   ├── yyjson/
│   └── clips/
├── src/                        # Handwritten C
├── docs/
│   └── schema.md               # Generated, drift-gated
└── foss-visual/                # Ring 2 stack
    └── specs/
        ├── statesmith.schema
        ├── protobuf.schema
        ├── eez-studio.schema
        ├── openmodelica.schema
        └── wasm.schema
```

---

## Make Targets

```makefile
# ── Ring 0 Generators ─────────────────────────────────────────
regen:          # Run all generators
gen-schema:     schemagen specs/*.schema → gen/
gen-def:        defgen specs/*.def → gen/
gen-sm:         smgen specs/*.sm → gen/
gen-lex:        lexgen specs/*.lex → gen/
gen-parser:     lemon specs/*.grammar → gen/
gen-feature:    bddgen specs/*.feature → gen/
gen-embed:      bin2c assets/* → gen/

# ── Ring 1 Utilities ──────────────────────────────────────────
gen-cli:        gengetopt specs/*.ggo → gen/
gen-headers:    makeheaders src/*.c → gen/
lint:           cppcheck src/
sanitize:       make CC="cc -fsanitize=address,undefined"

# ── Ring 2 Imports ────────────────────────────────────────────
import-models:  # Run only if tools installed
import-statesmith: dotnet StateSmith.Cli run model/*.drawio → gen/
import-proto:   protoc --c_out=gen/ model/*.proto
import-eez:     eez-studio-build model/*.eez-project → gen/
import-modelica: omc model/*.mo → gen/

# ── Meta Targets ──────────────────────────────────────────────
all:            regen compile
check:          unit tests + sanitizer builds
check-drift:    regen && git diff --exit-code gen/ docs/
ape:            PROFILE=ape build
native:         PROFILE=portable build
```

---

## The "Web Dev Ease" Comparison

| Web Dev Has... | This Stack Has... |
|----------------|-------------------|
| `npm install` | `cp lib.h vendors/libs/` |
| Hot reload | `make && ./app.com` (< 1s) |
| Package.json | Makefile (simpler) |
| React components | Nuklear panels (less code) |
| REST API | mongoose.h (~50 LOC setup) |
| PostgreSQL | sqlite3.c (zero-config) |
| JSON.parse() | yyjson (3 lines) |
| TypeScript types | `struct` + compiler warnings |
| Docker | Single `.com` binary |
| CI/CD | `sh test.sh && scp app.com server:` |

---

## State Machine Pattern (Replaces mbeddr)

Express state machines as **data**:

```c
// X-macro generated enums
#define STATES X(IDLE) X(RUNNING) X(ERROR)
#define EVENTS X(START) X(STOP) X(FAULT)

#define X(name) STATE_##name,
enum state { STATES STATE_COUNT };
#undef X

// Transition table — this IS the spec
typedef struct {
    enum state from;
    enum event on;
    enum state to;
    void (*action)(void);
} transition;

static transition table[] = {
    { STATE_IDLE,    EVENT_START, STATE_RUNNING, on_start  },
    { STATE_RUNNING, EVENT_STOP,  STATE_IDLE,    on_stop   },
    { STATE_RUNNING, EVENT_FAULT, STATE_ERROR,   on_fault  },
};

// 10-line dispatcher
void dispatch(enum event e) {
    for (int i = 0; i < countof(table); i++) {
        if (table[i].from == current_state && table[i].on == e) {
            if (table[i].action) table[i].action();
            current_state = table[i].to;
            return;
        }
    }
}
```

---

## Code Generation Pattern (Replaces AutoGen)

Use sh + awk instead of Guile Scheme:

```sh
#!/bin/sh
# gen_commands.sh
echo "// AUTO-GENERATED"
echo '#include "commands.h"'
echo "command_t commands[] = {"
while read name func desc; do
    echo "    { \"$name\", $func, $desc },"
done < commands.def
echo "    { NULL, NULL, NULL }"
echo "};"
```

Makefile:
```makefile
commands.c: commands.def gen_commands.sh
    sh gen_commands.sh > $@
```

---

## What's Explicitly Excluded and Why

| Excluded | Reason |
|----------|--------|
| MetaEdit+ | Java-based, GUI-only, proprietary |
| mbeddr | JetBrains MPS (Java/Kotlin) |
| AutoGen (with Guile) | Guile is Scheme — use M4 + X-macros |
| CMake | C++-like DSL, unnecessary |
| Meson | Python-based |
| Coccinelle | OCaml (use sed/awk or binary) |
| Protobuf (full) | protoc is C++ (use flatcc) |
| Any Haskell/OCaml/Rust | Violates C+sh constraint |

**Philosophy:** If it isn't C or sh, it isn't in the critical path.

---

## Reconciliation Summary

The three stacks are unified by the **Ring System**:

1. **Ring 0** (Pure C+sh) is the foundation that MUST build everything
2. **Ring 1** (C utilities) enhances velocity but has fallbacks
3. **Ring 2** (External toolchains) provides visual productivity but outputs are committed

This means:
- You CAN use Rhapsody/Simulink/StateSmith/EEZ/OpenModelica
- Their outputs go into `gen/` and are **drift-gated**
- A clean checkout with only `cc`, `make`, `sh` still builds
- Commercial tools become optional "super-generators"

**Result:** Strict build + Optional super-generators = Best of both worlds.
