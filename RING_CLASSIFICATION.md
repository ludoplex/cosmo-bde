# Ring Classification

> **BDE with Models** — Behavior Driven Engineering with Models.
>
> **THIS DOCUMENT IS REQUIRED READING** — See INTEROP_MATRIX.md for ground truth.

Tools and dependencies are classified into three rings based on their bootstrap requirements and toolchain dependencies. All rings output C code that compiles with cosmocc to Actually Portable Executables (APE).

## Universal Portability via Cosmopolitan

All Ring 0 and Ring 1 tools are built with [cosmocc](https://github.com/jart/cosmopolitan) to produce APE binaries that run on:
- Linux (x86_64, aarch64)
- macOS (x86_64, arm64)
- Windows (x86_64)
- FreeBSD, OpenBSD, NetBSD (x86_64)

```bash
# Install the pinned cosmocc (one-time; version + sha256 live in cosmocc.mk)
make toolchain
export PATH="$PWD/.cosmocc/4.0.2/bin:$PATH"

# Build portable tools
make tools    # Ring 0: schemagen, lemon (APE)
make ring1    # Ring 1: makeheaders (APE)
make app      # Application (APE)
```

Reference: [jart/cosmopolitan](https://github.com/jart/cosmopolitan)

---

## Classification Criteria

| Ring | Bootstrap Requirement | Toolchain | Outputs |
|------|----------------------|-----------|---------|
| **Ring 0** | C compiler + sh + make | Pure C | May be regenerated or committed |
| **Ring 1** | Ring 0 + optional C tools | C + utilities | Regenerated on demand |
| **Ring 2** | External toolchains | C++, .NET, Java, MATLAB, etc. | **Must be committed** |

---

## Ring 0: Bootstrap Layer

**Requirement**: Must build from clean checkout with only `sh`, `make`, and a C compiler.

### In-Tree Generators

| Tool | Purpose | Source |
|------|---------|--------|
| `schemagen.c` | Generate struct definitions, serializers, validators from `schema.def` | Local |
| `lexgen.c` | Generate table-driven lexer from token definitions | Local |
| `bin2c.c` | Embed binary resources as C arrays | Local |

### Vendored Libraries (C source, compile directly)

| Library | License | Purpose | Source |
|---------|---------|---------|--------|
| SQLite | Public Domain | Embedded database | [sqlite.org](https://sqlite.org) |
| Lemon | Public Domain | LALR(1) parser generator | [sqlite.org/lemon.html](https://sqlite.org/lemon.html) |
| CivetWeb | MIT | Embedded HTTP server | [GitHub](https://github.com/civetweb/civetweb) |
| Nuklear | MIT/Public Domain | Immediate-mode GUI | [GitHub](https://github.com/Immediate-Mode-UI/Nuklear) |
| yyjson | MIT | Fast JSON parser | [GitHub](https://github.com/ibireme/yyjson) |
| CLIPS | Public Domain | Rules/expert system engine | [clipsrules.net](https://www.clipsrules.net) |

### Build Tools

| Tool | Purpose |
|------|---------|
| `sh` | Shell scripting |
| `make` | Build orchestration |
| `cc` | C compiler (gcc, clang, tcc, cosmocc) |

### Cosmopolitan/APE (Optional Profile)

| Tool | License | Notes |
|------|---------|-------|
| cosmocc | ISC | Cross-platform C compiler |
| APE | ISC | Actually Portable Executable format |

**Note**: APE binaries run on Linux, macOS, Windows, FreeBSD, OpenBSD, NetBSD on AMD64 and ARM64. Linux may require APE loader installation for smoother execution.

---

## Ring 1: Velocity Tools (Portable)

**Requirement**: Ring 0 + additional C-based tools that enhance productivity but aren't strictly required.

**Rule**: If a Ring-1 tool is unavailable, the build MUST still succeed (with reduced functionality or skipped checks).

**Portability**: Ring 1 tools are built with cosmocc to APE format for universal OS support.

### Ring 1 Tool Inventory

| Tool | Source | Build | Format | Capability | Portable |
|------|--------|-------|--------|------------|----------|
| makeheaders | Vendored | `make ring1` | `.c` → `.h` | Auto-generate headers | ✓ APE |
| gengetopt | Vendored | `make ring1` | `.ggo` → `.c` | CLI parser generator | ✓ APE |
| cppcheck | System | see below | `.c` → report | Static analysis | ○ Native |
| ASan/UBSan | Compiler | flags | runtime | Memory/UB detection | ✓ cosmocc |
| TSan | Compiler | flags | runtime | Data race detection | ✓ cosmocc |

### Portable Ring 1 Tools (APE Binaries)

Tools in `tools/ring1/` are built with cosmocc to produce APE binaries:

```bash
# Build with cosmocc (portable)
make ring1              # Auto-detects cosmocc

# Or explicitly
CC=cosmocc make ring1   # Force cosmocc

# Resulting APE binaries work everywhere:
./build/makeheaders     # Linux, macOS, Windows, BSD
./build/gengetopt       # Linux, macOS, Windows, BSD
```

### Cosmopolitan Prebuilts Reference

Some tools are available as prebuilt APE binaries from cosmopolitan:

| Tool | Prebuilt | Source |
|------|----------|--------|
| make | [cosmo.zip/pub/cosmos/bin/make](https://cosmo.zip/pub/cosmos/bin/make) | GNU Make as APE |
| gcc | [cosmo.zip/pub/cosmos/bin/gcc](https://cosmo.zip/pub/cosmos/bin/gcc) | GCC as APE |
| vim | [cosmo.zip/pub/cosmos/bin/vim](https://cosmo.zip/pub/cosmos/bin/vim) | Vim as APE |

See: [cosmo.zip/pub/cosmos/bin/](https://cosmo.zip/pub/cosmos/bin/) for full list.

### Tool Capabilities

#### makeheaders (Vendored)
**Location**: `tools/ring1/makeheaders/makeheaders.c`
**Build**: `make ring1`
**Purpose**: Scans C source files and auto-generates corresponding header files with exported function declarations.

```bash
# Usage
build/makeheaders src/*.c gen/domain/*.c

# Capability
# - Finds functions without 'static' keyword
# - Generates .h with prototypes
# - Avoids manual header maintenance
```

#### gengetopt (System)
**Install**: `apt install gengetopt`
**Format**: `.ggo` (gengetopt option file)
**Purpose**: Generates C code for parsing command-line arguments.

```bash
# Example .ggo spec (specs/interface/myapp.ggo)
package "myapp"
version "1.0"
option "verbose" v "Enable verbose output" flag off
option "config"  c "Config file path" string optional

# Generated
gen/interface/myapp_cli.c
gen/interface/myapp_cli.h
```

**Capability**: Type-safe CLI parsing, automatic --help/--version, man page generation.

#### cppcheck (System)
**Install**: `apt install cppcheck`
**Purpose**: Static analysis for C/C++ code.

```bash
# Usage
make lint

# Capability
# - Detects null pointer dereferences
# - Finds memory leaks
# - Identifies undefined behavior
# - Style and portability warnings
```

#### Sanitizers (Compiler Built-in)
**Requirement**: GCC 4.8+ or Clang 3.1+
**Purpose**: Runtime error detection.

```bash
# AddressSanitizer + UBSan
make sanitize
# Detects: buffer overflows, use-after-free, memory leaks
# Detects: integer overflow, null dereference, alignment issues

# ThreadSanitizer
make tsan
# Detects: data races, deadlocks
```

### Ring 1 Files

```
tools/ring1/
├── VENDORS.txt              # External tool sources
└── makeheaders/
    └── makeheaders.c        # Vendored from SQLite

specs/interface/
└── *.ggo                    # CLI specs for gengetopt
```

### Ring 1 Make Targets

```makefile
make ring1      # Build vendored Ring 1 tools (makeheaders)
make headers    # Run makeheaders on src/*.c
make lint       # Run cppcheck static analysis
make sanitize   # Build with ASan + UBSan
make tsan       # Build with ThreadSanitizer
```

### Ring 1 in regen-all.sh

The regen script auto-detects Ring 1 tools:
```
── Ring 1: Velocity tools (auto-detected) ──────────────────────────────────
   (Ring 0 + optional C tools)
[makeheaders] Scanning src/*.c for exportable functions...
[gengetopt] Processing specs/**/*.ggo...
[cppcheck] Available for static analysis (run: make lint)
```

---

## Ring 2: Authoring Appliances

**Requirement**: These tools require non-C toolchains. Their **outputs must be committed** to the repository.

### Ring 2 Spec Files

```
foss-visual/specs/
├── statesmith.schema        # StateSmith configuration
├── protobuf.schema          # Protocol Buffer structure
├── eez-studio.schema        # EEZ UI definition
├── openmodelica.schema      # Modelica model structure
├── wasm.schema              # Binaryen + WAMR configuration
└── examples/
    ├── blinker.drawio.notes # StateSmith example notes
    ├── message.proto        # Protobuf example
    └── pendulum.mo          # Modelica example
```

### State Machine Generators

| Tool | Toolchain Required | Generated Code | License | Spec File |
|------|--------------------|----------------|---------|-----------|
| StateSmith | .NET (C#) | Zero-dependency C | Apache-2.0 | `statesmith.schema` |
| IBM Rhapsody | IBM installation | C/C++ | Commercial | - |
| Simulink/Stateflow | MATLAB | C/C++ (via Embedded Coder) | Commercial | - |

#### StateSmith Notes
- Tool is implemented in C# (see [GitHub language breakdown](https://github.com/StateSmith/StateSmith))
- Generated code has **zero runtime dependencies**
- Human-readable output
- Perfect for Ring-2 authoring → Ring-0 generated code

### Data/Schema Generators

| Tool | Toolchain Required | Generated Code | License |
|------|--------------------|----------------|---------|
| protobuf-c | C++ compiler, protobuf, pkg-config | C serialization code | BSD-2-Clause |
| rtiddsgen (RTI DDS) | Java | Type support code from IDL/XML/XSD | Commercial |
| OpenModelica | C++ (build), OCaml (parts) | C simulation code | OSMC-PL |

#### protobuf-c Notes
Per the [build documentation](https://github.com/protobuf-c/protobuf-c):
> Requires a C compiler, a C++ compiler, protobuf, and pkg-config

This is why it's Ring-2, not Ring-0.

#### rtiddsgen Notes
Per [RTI documentation](https://community.rti.com/static/documentation/connext-dds/6.1.2/doc/api/connext_dds/api_c/group__DDSNddsgenModule.html):
- Generates **type support code** from IDL/XML/XSD
- Includes: allocation, send/receive helpers, printing utilities
- Does **not** generate the entire networking stack—you still integrate DDS runtime + QoS config

### UI/Visual Editors

| Tool | Toolchain Required | Generated Code | License |
|------|--------------------|----------------|---------|
| EEZ Studio | Node.js/Electron | C or **C++** (template-dependent) | GPL-3.0 |
| Qt Design Studio | Qt installation | QML/C++ | Commercial/GPL |

#### EEZ Studio Notes
Per the [README](https://github.com/eez-open/studio):
- License: **GPL-3.0**
- Can generate **C++ code** for embedded GUI (plan for C++ compiler or verify C-only output)
- EEZ does not claim ownership over generated code (with caveats around EEZ Flow)
- Generated file licensing depends on templates used (MIT/BSD/PD possible)

### Modeling/Simulation

| Tool | Toolchain Required | Generated Code | License | Spec File |
|------|--------------------|----------------|---------|-----------|
| OpenModelica | C++ compiler | C code | OSMC-PL | `openmodelica.schema` |
| Simulink Coder | MATLAB | C/C++ | Commercial | - |
| Embedded Coder | MATLAB | Production C/C++ | Commercial | - |

#### OpenModelica Notes
Per the [scripting documentation](https://build.openmodelica.org/Documentation/OpenModelica.Scripting.html):
- `translateModel` → translate Modelica to **C code**
- `buildModel` → translate to **C** and build executable

### WebAssembly Tools

| Tool | Toolchain Required | Purpose | License | Spec File |
|------|--------------------|---------|---------|-----------|
| Binaryen | C++ compiler | WASM optimization | Apache-2.0 | `wasm.schema` |
| WAMR (interp) | C only! | WASM runtime | Apache-2.0 | `wasm.schema` |

#### WAMR Notes
WAMR in interpreter mode is **Ring 0 compatible**! It compiles with cosmocc and runs WASM modules portably. The AOT/JIT modes require platform-specific code.

---

## BDD Feature Files

Each ring has corresponding BDD test scenarios:

```
.claude/features/
├── schemagen.feature   # Ring 0: Schema generator
├── defgen.feature      # Ring 0: Definition generator
├── lemon.feature       # Ring 0: Parser generator
├── ring1.feature       # Ring 1: gengetopt, cppcheck, sanitizers
└── ring2.feature       # Ring 2: StateSmith, protobuf, EEZ, WASM
```

Run BDD tests:
```bash
./build/bddgen --run .claude/features/
```

---

## Ring Assignment Decision Tree

```
Is the tool written in pure C and buildable with cc+sh+make?
├─ YES → Ring 0
└─ NO
   ├─ Is it a C-based utility that enhances but isn't required?
   │  ├─ YES → Ring 1
   │  └─ NO → Ring 2
   └─ Does it require C++, Java, .NET, MATLAB, or similar?
      └─ YES → Ring 2 (outputs must be committed)
```

---

## Adding New Tools

When adding a new tool:

1. Determine its ring classification using the decision tree
2. Document in this file with:
   - Toolchain requirements
   - License
   - What it generates
   - Source/documentation links
3. If Ring-2:
   - Set up `gen/<tool>/` directory structure
   - Add stamp files (`GENERATOR_VERSION`, `SHA256SUMS`)
   - Commit all generated outputs
4. Update `LICENSES.md` with license details

<!--
One Ring to bootstrap them all, One Ring to bind them,
One Ring to make regen and in the diff gate find them,
In the Land of Cosmopolitan where strange APE aeon binaries lie.

                    ▄▄▄▄▄▄▄▄▄
                   █░░░░░░░░░█
                   █ ◉     ◉ █
                   █    ▲    █        psst...
                   █ ╲_____╱ █    ╭────────────╮
                   █▄▄▄▄▄▄▄▄▄█ ── │ ...I'm    │
                        ║        │ voldemort! │
                   ╔════╩════╗   ╰────────────╯
                   ║  RING   ║        🐍
                   ║    0    ║
                   ╚═════════╝
-->