# cosmo-bde Architecture

> **LLM Reference Document** - Comprehensive system architecture for LLM-based coding assistants.
>
> See also: `RING_CLASSIFICATION.md`, `INTEROP_MATRIX.md`, `LITERATE.md`

---

## 1. System Overview

```
+-----------------------------------------------------------------------------+
|                          cosmo-bde                                     |
|                    "Behavior Driven Engineering with Models"                 |
+-----------------------------------------------------------------------------+
|                                                                              |
|   PRINCIPLE: Directory Structure = Format Graph = Build Rules = Docs        |
|                                                                              |
|   +-------------------------------------------------------------------+     |
|   |                        SPEC FILES (SSOT)                          |     |
|   |                                                                   |     |
|   |   specs/                                                          |     |
|   |   +-- domain/           <- .schema files (entity definitions)     |     |
|   |   +-- behavior/         <- .sm, .hsm files (state machines)       |     |
|   |   +-- interface/        <- .ggo, .api files (CLI, API contracts)  |     |
|   |   +-- parsing/          <- .lex, .grammar files (lexers, parsers) |     |
|   |   +-- testing/          <- .feature files (BDD scenarios)         |     |
|   +-------------------------------------------------------------------+     |
|                                    |                                         |
|                                    v                                         |
|   +-------------------------------------------------------------------+     |
|   |                     RING 0 GENERATORS (Pure C)                    |     |
|   |                     Built with: CC=cosmocc make tools             |     |
|   |                                                                   |     |
|   |   build/                                                          |     |
|   |   +-- schemagen   <- .schema -> _types.c,h / _json / _sql / .proto|     |
|   |   +-- defgen      <- .def (TOK/TABLE) -> _tokens.h / _model.h     |     |
|   |   +-- smgen       <- .sm -> _sm.c,h (state machines)              |     |
|   |   +-- lexgen      <- .lex -> _lex.c,h (lexers)                    |     |
|   |   +-- lemon       <- .grammar -> _parse.c,h (LALR parsers)        |     |
|   |   +-- bddgen      <- .feature -> _bdd.c (BDD test harness)        |     |
|   +-------------------------------------------------------------------+     |
|                                    |                                         |
|                                    v                                         |
|   +-------------------------------------------------------------------+     |
|   |                      GENERATED CODE (Committed)                   |     |
|   |                                                                   |     |
|   |   gen/                                                            |     |
|   |   +-- domain/           <- _types.c,h, _json.c,h, _sql.c,h        |     |
|   |   +-- behavior/         <- _sm.c,h                                |     |
|   |   +-- interface/        <- _cli.c,h                               |     |
|   |   +-- parsing/          <- _lex.c,h, _parse.c,h                   |     |
|   |   +-- testing/          <- _bdd.c                                 |     |
|   |   +-- imported/         <- Ring 2 tool outputs                    |     |
|   |   +-- REGEN_TIMESTAMP   <- Drift detection marker                 |     |
|   +-------------------------------------------------------------------+     |
|                                    |                                         |
|                                    v                                         |
|   +-------------------------------------------------------------------+     |
|   |                       APPLICATION CODE                            |     |
|   |                                                                   |     |
|   |   src/                  <- #include "gen/domain/*_types.h"        |     |
|   |   +-- main.c                                                      |     |
|   |   +-- app.c                                                       |     |
|   +-------------------------------------------------------------------+     |
|                                    |                                         |
|                                    v                                         |
|   +-------------------------------------------------------------------+     |
|   |                         APE BINARY                                |     |
|   |              (Actually Portable Executable)                       |     |
|   |                                                                   |     |
|   |   build/app   <- Runs on Linux, macOS, Windows, BSD               |     |
|   +-------------------------------------------------------------------+     |
|                                                                              |
+-----------------------------------------------------------------------------+
```

---

## 2. APE Binary Structure

APE (Actually Portable Executable) is a polyglot binary format that is simultaneously:
- DOS/Windows PE executable
- Unix shell script
- ELF binary (Linux/BSD)
- Mach-O binary (macOS)
- ZIP archive (ZipOS virtual filesystem)

```
+-----------------------------------------------------------------------------+
|                        APE FILE LAYOUT                                       |
+-----------------------------------------------------------------------------+
| Offset    | Content                                                          |
+-----------------------------------------------------------------------------+
| 0x0000    | MZ Header (0x4D 0x5A) + Shell Script Polyglot                    |
|           |   "MZqFpD='...'" <- Valid DOS stub AND shell variable            |
|           |   exec 7<> "$0"  <- Shell opens self, extracts native format     |
+-----------+------------------------------------------------------------------+
| 0x003C    | e_lfanew -> PE Header Offset                                     |
+-----------+------------------------------------------------------------------+
| 0x0080    | PE Header (PE\0\0) <- GROUND TRUTH FOR SECTIONS                  |
|           |   +-- COFF Header (machine, sections, timestamp)                 |
|           |   +-- Optional Header (entry point, image base 0x400000)         |
|           |   +-- Section Headers (.text, .data, .rdata, .bss)               |
+-----------+------------------------------------------------------------------+
| varies    | ELF Header (0x7F ELF) <- Embedded in shell comment region        |
|           |   Program headers -> point to same .text/.data                   |
+-----------+------------------------------------------------------------------+
| varies    | Mach-O Header (0xFEEDFACF) <- For macOS                          |
+-----------+------------------------------------------------------------------+
| varies    | .text Section (x86-64 machine code)                              |
|           |   +-- Cosmopolitan runtime startup                               |
|           |   +-- Application code (PIC)                                     |
+-----------+------------------------------------------------------------------+
| varies    | .data / .rodata / .bss Sections                                  |
+-----------+------------------------------------------------------------------+
| varies    | ZipOS Virtual Filesystem                                         |
|           |   /zip/assets/...     <- Zero-copy mmap (STORE compression)      |
|           |   /zip/binaryen.wasm  <- Embedded WASM for object diffing        |
|           |   /zip/src/...        <- Source files for self-patching          |
+-----------+------------------------------------------------------------------+
| EOF       | ZIP End of Central Directory                                     |
+-----------------------------------------------------------------------------+

KEY INSIGHT: PE sections are the authoritative layout for APE.
             No ELF sections exist for x86-64 APE binaries.
```

---

## 3. Ring Classification

```
+-----------------------------------------------------------------------------+
|                           RING CLASSIFICATION                                |
|                                                                              |
|   "Ring 2 outputs committed, builds succeed with Ring 0 only"               |
+-----------------------------------------------------------------------------+

+=============================================================================+
||  RING 0: Bootstrap (C + sh + make)                                        ||
||  --------------------------------------------------------------------------||
||  ALWAYS AVAILABLE - Builds with only: cc, make, sh                        ||
||                                                                            ||
||  Generators:  schemagen, defgen, smgen, lexgen, lemon, bddgen             ||
||  Libraries:   SQLite, yyjson, Nuklear, CLIPS                              ||
||  Tools:       cosmocc (APE compiler)                                       ||
+=============================================================================+
                                  |
                                  | generates
                                  v
+=============================================================================+
||  RING 1: Ring 0 + C Velocity Tools                                        ||
||  --------------------------------------------------------------------------||
||  Enhanced development, optional for production builds                      ||
||                                                                            ||
||  Tools:       gengetopt, cppcheck, sanitizers, valgrind                   ||
||  Features:    CLI generation, static analysis, memory debugging           ||
+=============================================================================+
                                  |
                                  | outputs committed
                                  v
+=============================================================================+
||  RING 2: External Toolchains                                              ||
||  --------------------------------------------------------------------------||
||  Designer tools, outputs checked into repo                                 ||
||                                                                            ||
||  Tools:       StateSmith (.drawio -> _sm.c)                               ||
||               protoc (.proto -> .pb-c.c)  <- FROM schemagen .proto        ||
||               flatcc (.fbs -> _fb.c)      <- FROM schemagen .fbs          ||
||               MATLAB/Simulink, Rhapsody                                   ||
||                                                                            ||
||  KEY: Ring 2 OUTPUTS are committed, not the tools                         ||
+=============================================================================+

COMPOSABILITY EXAMPLE:
.schema --[schemagen]--> .proto --[protoc]--> .pb-c.c
   |                        |                     |
   |                        |                     +-- Ring 2 output (committed)
   |                        +-- Ring 0 output (committed)
   +-- SSOT (Single Source of Truth)

Result: Build succeeds with Ring 0 only using committed .pb-c.c
```

---

## 4. Code Generation Flow

```
+-----------------------------------------------------------------------------+
|                         CODE GENERATION PIPELINE                             |
+-----------------------------------------------------------------------------+

   SOURCE SPEC                   GENERATOR              OUTPUT
   -----------                   ---------              ------

   specs/domain/
   +-- example.schema  ------>  schemagen  ------>  gen/domain/
                                   |                +-- example_types.c
                                   |                +-- example_types.h
                                   |                +-- example_json.c
                                   |                +-- example_json.h
                                   |                +-- example_sql.c
                                   |                +-- example_sql.h
                                   |                +-- example.proto
                                   +--------------> +-- example.fbs

   specs/parsing/
   +-- tokens.def  ---------->  defgen  -------->  gen/parsing/
                                                   +-- tokens_tokens.h
   +-- model.def  ----------->  defgen  -------->  +-- model_model.h

   specs/behavior/
   +-- door.sm  ------------>  smgen  --------->  gen/behavior/
                                                   +-- door_sm.c
                                                   +-- door_sm.h

   specs/parsing/
   +-- config.lex  ---------->  lexgen  ------->  gen/parsing/
                                                   +-- config_lex.c
                                                   +-- config_lex.h
   +-- config.grammar  ------>  lemon  -------->  +-- config_parse.c
                                                   +-- config_parse.h

   specs/testing/
   +-- sensor.feature  ------>  bddgen  ------->  gen/testing/
                                                   +-- sensor_bdd.c

+-----------------------------------------------------------------------------+
|                         NAMING CONVENTION                                    |
+-----------------------------------------------------------------------------+

   Pattern: {name}_{role}.{ext}

   +----------+------------+-----------------------+
   | Role     | Suffix     | Example               |
   +----------+------------+-----------------------+
   | Types    | _types     | sensor_types.c        |
   | State    | _sm        | door_sm.c             |
   | Parser   | _parse     | config_parse.c        |
   | Lexer    | _lex       | config_lex.c          |
   | BDD      | _bdd       | sensor_bdd.c          |
   | JSON     | _json      | sensor_json.c         |
   | SQL      | _sql       | sensor_sql.c          |
   | Tokens   | _tokens    | config_tokens.h       |
   | Model    | _model     | data_model.h          |
   +----------+------------+-----------------------+
```

---

## 5. X-Macro Pattern

X-macros provide "define once, expand everywhere" functionality for constants, enums, and tables.

```
+-----------------------------------------------------------------------------+
|                           X-MACRO PATTERN                                    |
+-----------------------------------------------------------------------------+

DEFINITION (in .def file or header):
+----------------------------------------------------------------------+
|  #define PROCMEM_OS_XMACRO(X) \                                       |
|      X(PROCMEM_OS_UNKNOWN, 0, "unknown") \                            |
|      X(PROCMEM_OS_LINUX,   1, "linux")   \                            |
|      X(PROCMEM_OS_WINDOWS, 2, "windows") \                            |
|      X(PROCMEM_OS_MACOS,   3, "macos")   \                            |
|      X(PROCMEM_OS_BSD,     4, "bsd")                                  |
+----------------------------------------------------------------------+

EXPANSION TO CONSTANTS:
+----------------------------------------------------------------------+
|  #define X(name, val, str) static const int name = val;              |
|  PROCMEM_OS_XMACRO(X)                                                 |
|  #undef X                                                             |
|                                                                       |
|  // Expands to:                                                       |
|  // static const int PROCMEM_OS_UNKNOWN = 0;                          |
|  // static const int PROCMEM_OS_LINUX = 1;                            |
|  // static const int PROCMEM_OS_WINDOWS = 2;                          |
|  // static const int PROCMEM_OS_MACOS = 3;                            |
|  // static const int PROCMEM_OS_BSD = 4;                              |
+----------------------------------------------------------------------+

EXPANSION TO STRING CONVERTER:
+----------------------------------------------------------------------+
|  static inline const char* procmem_os_str(int os) {                  |
|      switch(os) {                                                     |
|  #define X(name, val, str) case val: return str;                     |
|      PROCMEM_OS_XMACRO(X)                                             |
|  #undef X                                                             |
|      }                                                                |
|      return "unknown";                                                |
|  }                                                                    |
+----------------------------------------------------------------------+

BENEFIT: Single source of truth for constants, strings, and values.
         Change the XMACRO definition, all expansions update automatically.
```

---

## 6. Live Reload Architecture

See `docs/APE_LIVERELOAD.md` for detailed live reload documentation.

```
+-----------------------------------------------------------------------------+
|                        LIVE RELOAD WORKFLOW                                  |
+-----------------------------------------------------------------------------+

   Terminal 1              Terminal 2              Terminal 3
   ----------              ----------              ----------

   +---------------+      +---------------+      +---------------+
   |  ./build/app  |      |  sudo         |      |  vim src/app.c|
   |               |      |  ./build/     |      |               |
   |  Running...   |      |  livereload   |      |  Edit function|
   |  Message: Hello|     |  $(pgrep app) |      |  :w (save)    |
   |               |<-----|  src/app.c    |<-----|               |
   |  Message: World|100ms|  [OK] Patched |inotify|              |
   |  ^            |      |  get_message  |      |               |
   |  +-- Changed! |      |  @ 0x401234   |      |               |
   +---------------+      +---------------+      +---------------+

+-----------------------------------------------------------------------------+
|                         UNDER THE HOOD                                       |
+-----------------------------------------------------------------------------+

   1. File Watch     <- inotify/kqueue/stat() detects IN_CLOSE_WRITE
          |
          v
   2. Compile        <- cosmocc -c src/app.c -o .e9cache/app.new.o
          |
          v
   3. Diff           <- e9_binaryen_diff(.e9cache/app.o, .e9cache/app.new.o)
          |
          v
   4. Translate      <- e9_ape_rva_to_offset() converts PE RVA to file offset
          |
          v
   5. Patch          <- process_vm_writev(pid, new_bytes, addr)
          |                 (Linux: no ptrace needed!)
          v
   6. Flush          <- __builtin___clear_cache(addr, addr + size)
          |
          v
   7. Continue       <- Running process executes new code immediately

   Total latency: ~200-500ms (dominated by compilation)
```

---

## 7. CI/CD Pipeline

```
+-----------------------------------------------------------------------------+
|                           CI/CD PIPELINE                                     |
+-----------------------------------------------------------------------------+

   .github/actions/setup-cosmocc/    (Composite Action - Shared)
   +--------------------------------------------------------------------+
   |  steps:                                                             |
   |    - Cache ~/.cosmocc (key: cosmocc-${{ runner.os }})              |
   |    - Download cosmocc.zip if cache miss                             |
   |    - Add ~/.cosmocc/bin to PATH                                     |
   +--------------------------------------------------------------------+
                                  |
                +---------------- | ----------------+
                |                 |                 |
                v                 v                 v
   +-------------------+ +-------------------+ +-------------------+
   |    repo-ci.yml    | |  template-ci.yml  | |      ci.yml       |
   |  (Meta Repo Dev)  | |  (Template Users) | |   (Main Build)    |
   +-------------------+ +-------------------+ +-------------------+
   |                   | |                   | |                   |
   | Jobs:             | | Jobs:             | | Jobs:             |
   | +-- generators    | | +-- build         | | +-- build (APE)   |
   | |   Test schemagen| | |   make tools    | | |   cosmocc       |
   | |   Test --all    | | |   make app      | | |   make tools    |
   | |                 | | |   make run      | | |   make verify   |
   | +-- composability | | |                 | | |   make app      |
   | |   Test --proto  | | +-- drift         | | |   make e9studio |
   | |   Test --fbs    | | |   make verify   | | |                 |
   | |                 | | |                 | | +-- native        |
   | +-- build         | | +-- ape (main)    | | |   System cc     |
   | +-- meta-tests    | | |   Upload artifact| |   make run      |
   | +-- ape-build     | | |                 | |                   |
   | +-- docs          | | +-- bdd           | |                   |
   | +-- template-init | | |   Feature files | |                   |
   | +-- e9studio      | | |                 | |                   |
   |     Integration   | | +-- livereload    | |                   |
   |                   | | |   Build + test  | |                   |
   +-------------------+ +-------------------+ +-------------------+

   DRIFT VERIFICATION (scripts/verify.sh):
   +--------------------------------------------------------------------+
   |  1. make regen                    <- Regenerate all from specs     |
   |  2. git diff --exit-code gen/     <- Check for uncommitted changes |
   |     (exclude GENERATOR_VERSION, REGEN_TIMESTAMP)                   |
   |  3. Exit 1 if drift detected      <- CI fails, must commit gen/    |
   |                                                                     |
   |  Purpose: Ensure gen/ always matches specs/ (no hidden changes)    |
   +--------------------------------------------------------------------+
```

---

## 8. Meta Repository vs End-User Template

```
+-----------------------------------------------------------------------------+
|                    META REPO vs END-USER TEMPLATE                            |
+-----------------------------------------------------------------------------+

+===========================================================================+
|               cosmo-bde (Meta Repository)                            |
|               github.com/user/mbse-stacks                                  |
+---------------------------------------------------------------------------+
|   Purpose: Develop generators, test framework, maintain docs               |
|                                                                            |
|   +-- tools/                  <- Generator source code                     |
|   |   +-- schemagen.c                                                      |
|   |   +-- defgen.c                                                         |
|   |   +-- lempar.c                                                         |
|   |                                                                        |
|   +-- vendors/submodules/e9studio/      <- Live reload submodule                     |
|   |   +-- src/e9patch/                                                     |
|   |   +-- specs/                                                           |
|   |   +-- gen/                                                             |
|   |                                                                        |
|   +-- .github/workflows/                                                   |
|   |   +-- repo-ci.yml         <- Tests generators themselves               |
|   |   +-- template-ci.yml     <- CI for template users (copied)            |
|   |   +-- ci.yml              <- Main CI                                   |
|   |                                                                        |
|   +-- scripts/template-init.sh <- Transforms meta -> user project          |
|   +-- templates/project/       <- Template files                           |
+===========================================================================+
                                  |
                                  | "Use this template"
                                  | + scripts/template-init.sh
                                  v
+===========================================================================+
|               END-USER PROJECT (From Template)                             |
|               github.com/company/my-app                                    |
+---------------------------------------------------------------------------+
|   Purpose: Build actual application using the framework                    |
|                                                                            |
|   +-- specs/                  <- User's domain specs                       |
|   |   +-- domain/                                                          |
|   |       +-- myapp.schema    <- User creates these                        |
|   |                                                                        |
|   +-- gen/                    <- Generated from user specs                 |
|   |   +-- domain/                                                          |
|   |       +-- myapp_types.c,h                                              |
|   |       +-- myapp_json.c,h                                               |
|   |                                                                        |
|   +-- src/                    <- User's application code                   |
|   |   +-- main.c                                                           |
|   |                                                                        |
|   +-- build/                  <- Compiled tools + app                      |
|   |   +-- schemagen           <- Pre-built APE tool                        |
|   |   +-- app                 <- User's APE application                    |
|   |                                                                        |
|   +-- .github/workflows/                                                   |
|   |   +-- ci.yml              <- Renamed from template-ci.yml              |
|   |                                                                        |
|   +-- Makefile                <- Same make targets work                    |
+===========================================================================+

WORKFLOW IDENTICAL FOR BOTH:

   make tools   ->   make regen   ->   make verify   ->   make
       |                |                |               |
   Build Ring 0     Generate         Check drift      Build app
   generators       from specs       (CI gate)        (APE)
```

---

## 9. Universal Workflow

```bash
# The same commands work in meta repo and user projects:

make tools      # Build Ring 0 generators (schemagen, lemon, etc.)
make regen      # Regenerate all code from specs
make verify     # Regen + drift check (CI gate)
make            # Build application
make run        # Execute application
make test       # Run tests
make clean      # Clean build artifacts

# Live reload development:
make e9studio   # Build livereload tool
./build/app &   # Start application in background
sudo ./build/livereload $(pgrep app) src/main.c  # Attach live reload
# Edit src/main.c -> changes appear in real-time!
```

---

## 10. Key Files Reference

| Purpose | Path |
|---------|------|
| LLM Context | `.claude/CLAUDE.md` |
| Ring Classification | `RING_CLASSIFICATION.md` |
| Format Matrix | `INTEROP_MATRIX.md` |
| Literate System | `LITERATE.md` |
| Spec Types | `SPEC_TYPES.md` |
| This Document | `docs/ARCHITECTURE.md` |
| APE/LiveReload | `docs/APE_LIVERELOAD.md` |
| e9studio | `vendors/submodules/e9studio/` |

---

*Generated for LLM reference. Last updated: 2024*
