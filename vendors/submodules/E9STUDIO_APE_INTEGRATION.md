# E9Studio APE Binary In-Place Editing Integration

## Overview

This document describes the critical integration between e9studio, Cosmopolitan Binaryen, WAMR, and APE binary support for real-time C source-to-binary editing.

## Components

| Component | Location | Purpose |
|-----------|----------|---------|
| e9studio | `upstream/e9studio` | Binary analysis, CFG, decompilation, patching |
| cosmo-binaryen | `upstream/ludoplex-binaryen` | WASM optimizer (wasm-opt.com + binaryen.wasm) |
| cosmo-sokol | `upstream/cosmo-sokol` | GUI framework (Dear ImGui + sokol) |
| cosmo-teditor | `upstream/cosmo-teditor` | Editor framework with INI extensibility |
| cosmo-bsd | `upstream/ludoplex-cosmo-bsd` | APE userland, ZipOS utilities |

## APE Binary Format Support

APE (Actually Portable Executable) binaries are polyglot files simultaneously valid as:
- DOS MZ executable
- ELF (Linux/BSD)
- PE (Windows)
- Shell script (Unix shebang)
- ZIP archive (ZipOS embedded files)

### Detection (e9analysis.h)

```c
typedef enum {
    E9_FORMAT_UNKNOWN = 0,
    E9_FORMAT_ELF,
    E9_FORMAT_PE,
    E9_FORMAT_MACHO,
    E9_FORMAT_APE,      /* Actually Portable Executable */
    E9_FORMAT_RAW,
} E9Format;
```

### APE Layout Structure (E9Binary.ape)

```c
struct {
    uint64_t shell_offset;      /* Shell script shebang */
    uint64_t shell_size;
    uint64_t elf_offset;        /* ELF header */
    uint64_t elf_size;
    uint64_t pe_offset;         /* PE header */
    uint64_t pe_size;
    uint64_t macho_offset;      /* Mach-O (if present) */
    uint64_t macho_size;
    uint64_t zipos_start;       /* ZipOS content */
    uint64_t zipos_central_dir;
    uint64_t zipos_end;
    uint32_t zipos_num_entries;
} ape;
```

### APE API Functions

```c
bool e9_binary_is_ape(const uint8_t *data, size_t size);
int e9_binary_parse_ape(E9Binary *bin);
E9Binary *e9_ape_get_elf_view(E9Binary *bin);
E9Binary *e9_ape_get_pe_view(E9Binary *bin);
void e9_ape_free_view(E9Binary *view);
int e9_ape_patch_sync(E9Binary *bin, uint64_t vaddr,
                      const uint8_t *bytes, size_t size);
```

## Real-Time Binary Substitution Pipeline

```
┌─────────────────────────────────────────────────────────────────────┐
│                     E9Studio Hot-Reload Pipeline                     │
├─────────────────────────────────────────────────────────────────────┤
│                                                                      │
│  1. SOURCE EDITING (cosmo-teditor)                                   │
│     ┌─────────────┐     ┌─────────────┐                             │
│     │  main.c     │────▶│ File Watch  │                             │
│     │  (editing)  │     │   Event     │                             │
│     └─────────────┘     └──────┬──────┘                             │
│                                │                                     │
│  2. INCREMENTAL COMPILE        ▼                                     │
│     ┌─────────────┐     ┌─────────────┐                             │
│     │ cosmocc -c  │────▶│ main.o      │                             │
│     │ (modified)  │     │ (new)       │                             │
│     └─────────────┘     └──────┬──────┘                             │
│                                │                                     │
│  3. OBJECT DIFF               ▼                                      │
│     ┌─────────────┐     ┌─────────────┐     ┌─────────────┐         │
│     │ main.o      │     │ e9_diff_    │────▶│ E9PatchSet  │         │
│     │ (old)       │────▶│  objects()  │     │ (patches)   │         │
│     └─────────────┘     └─────────────┘     └──────┬──────┘         │
│                                                     │                │
│  4. WASM OPTIMIZATION (binaryen.wasm via WAMR)     ▼                │
│     ┌─────────────┐     ┌─────────────┐     ┌─────────────┐         │
│     │ BinaryenMod │     │ wasm-opt    │────▶│ Optimized   │         │
│     │ uleOptimize │────▶│ passes      │     │ patches     │         │
│     └─────────────┘     └─────────────┘     └──────┬──────┘         │
│                                                     │                │
│  5. APE IN-PLACE PATCHING                          ▼                │
│     ┌─────────────┐     ┌─────────────┐     ┌─────────────┐         │
│     │ Running APE │     │ e9_ape_     │────▶│ Patched APE │         │
│     │ binary      │────▶│ patch_sync  │     │ (live)      │         │
│     └─────────────┘     └─────────────┘     └─────────────┘         │
│                                                     │                │
│  6. ICACHE FLUSH                                   ▼                │
│     ┌─────────────────────────────────────────────────────┐         │
│     │ e9wasm_flush_icache() - processor sees new code     │         │
│     └─────────────────────────────────────────────────────┘         │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

## Building the Integration

### 1. Build Cosmopolitan Binaryen

```bash
cd upstream/ludoplex-binaryen
./cosmo/build.sh

# Outputs:
#   build-cosmo/bin/wasm-opt.com
#   build-cosmo/bin/wasm-as.com
#   build-cosmo/bin/wasm-merge.com
```

### 2. Build E9Studio with APE Support

```bash
cd upstream/e9studio
make -f Makefile.e9studio gui

# Outputs:
#   build/e9studio.com
#   build/e9studio-gui.com
```

### 3. Build WASM Payloads

```bash
cd upstream/e9studio/src/e9studio/wasm
make -f Makefile.wasm BINARYEN_PATH=../../ludoplex-binaryen

# Uses wasm-opt.com from binaryen build
```

## WAMR Integration

E9Studio embeds WAMR (WebAssembly Micro Runtime) with Fast JIT:

```c
// e9wasm_host.h API
int e9wasm_init(const E9WasmConfig *config);
void *e9wasm_load_module(const char *path);  // Load from ZipOS
int e9wasm_call(void *module, const char *func_name, int argc, const char *argv[]);

// Zero-copy binary access
void *e9wasm_mmap_binary(const char *zip_path, size_t *out_size, bool writable);
int e9wasm_apply_patch(void *mapped, size_t offset, const uint8_t *data, size_t size);
void e9wasm_flush_icache(void *addr, size_t size);
```

### WAMR Execution Modes

| Mode | Performance | Memory | Use Case |
|------|-------------|--------|----------|
| Interpreter | Moderate | Low | Portability testing |
| Fast JIT | High | Medium | Production |
| AOT | Highest | Varies | Pre-compiled modules |

## ZipOS Self-Modification

APE binaries can store and update their own ZipOS contents:

```c
// Store patched binary back into APE
int e9wasm_zipos_append(const char *name, const uint8_t *data, size_t size);

// List ZipOS contents
int e9wasm_zipos_list(E9WasmZipCallback callback, void *userdata);

// Read from ZipOS
uint8_t *e9wasm_zipos_read(const char *name, size_t *out_size);
```

## Key Implementation Files

| File | Purpose |
|------|---------|
| `e9studio/src/e9patch/analysis/e9analysis.c` | APE detection and parsing |
| `e9studio/src/e9patch/analysis/e9binpatch.c` | In-place binary editing |
| `e9studio/src/e9patch/analysis/e9polyglot.c` | Polyglot file analysis |
| `e9studio/src/e9patch/wasm/e9wasm_host.c` | WAMR integration |
| `e9studio/src/e9studio/gui/e9studio_gui.c` | GUI framework |
| `ludoplex-binaryen/cosmo/build.sh` | Cosmopolitan Binaryen build |

## Status

- [x] APE format detection (E9_FORMAT_APE)
- [x] APE layout parsing (E9Binary.ape struct)
- [x] ELF/PE view extraction from APE
- [x] Cosmopolitan Binaryen build system
- [x] WAMR host API
- [ ] APE synchronized patching (e9_ape_patch_sync)
- [ ] Live reload integration
- [ ] Complete GUI integration

## References

- [Cosmopolitan Libc](https://github.com/jart/cosmopolitan)
- [WAMR](https://github.com/bytecodealliance/wasm-micro-runtime)
- [Binaryen](https://github.com/WebAssembly/binaryen)
- [E9Patch](https://github.com/GJDuck/e9patch)
