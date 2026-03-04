# Vendor Repositories - Required Reading

> **LLM Agents: You MUST read and understand these repositories before writing code.**

## Priority 1: Core Dependencies (MUST READ)

### jart/cosmopolitan
**THE foundation. ALL code compiles with cosmocc.**

| File | Purpose |
|------|---------|
| README.md | Overview, philosophy, capabilities |
| tool/cosmocc/README.md | Compiler toolchain (GCC 14.1.0 + Clang 19) |
| ape/README.md | APE binary format |
| ape/loader.c | How APE loads on each platform |
| libc/calls/ | Cross-platform syscall wrappers |
| examples/ | Working examples |

**Critical knowledge:**
- PE sections are ground truth (not ELF for x86-64)
- cosmocc bundles GCC 14.1.0 (default) + Clang 19 (`-mclang`)
- ZipOS: `/zip/` virtual filesystem, zero-copy mmap
- Platform detection: `IsWindows()`, `IsLinux()`, `IsXnu()`, etc.

**URL:** https://github.com/jart/cosmopolitan

---

## Priority 2: Local Vendor Repos (in vendors/)

### ludoplex-binaryen
**WASM-based IR for object diffing. Cosmopolitan-compatible.**

| File | Purpose |
|------|---------|
| README.md | Build instructions, API overview |
| src/binaryen-c.h | C API |
| src/wasm/ | WASM module generation |

**Outputs:** `.com` (APE) + `.wasm` (embeddable in ZipOS)

**Local path:** `vendors/submodules/ludoplex-binaryen/`

---

### cosmo-sokol
**Cross-platform graphics/audio/input. Fork of bullno1/cosmo-sokol.**

| File | Purpose |
|------|---------|
| README.md | Build, platform support |
| sokol_app.h | Window/input |
| sokol_gfx.h | 3D graphics (Metal/D3D11/OpenGL/WebGPU) |
| sokol_audio.h | Audio playback |

**Local path:** `vendors/submodules/cosmo-sokol/`

---

### e9studio
**Binary patching for APE. This is what we're building.**

| File | Purpose |
|------|---------|
| .claude/CLAUDE.md | Agent context |
| docs/ARCHITECTURE.md | Component architecture |
| docs/IR_PATCHING.md | IR-based patching strategy |
| src/e9patch/ | Patching implementation |
| specs/ | Spec files (.schema, .sm, .lex, .grammar) |

**Local path:** `vendors/submodules/e9studio/`

---

### cosmo-bsd / ludoplex-cosmo-bsd
**BSD utilities ported to Cosmopolitan.**

**Local paths:** `vendors/cosmo-bsd/`, `vendors/ludoplex-cosmo-bsd/`

---

### cosmo-disasm
**Disassembler for APE binaries.**

**Local path:** `vendors/cosmo-disasm/`

---

### cosmo-gcc-plugin
**GCC plugin for Cosmopolitan-specific optimizations.**

**Local path:** `vendors/cosmo-gcc-plugin/`

---

### cosmogfx
**Graphics utilities for Cosmopolitan.**

**Local path:** `vendors/cosmogfx/`

---

### cosmo-include
**Additional headers for Cosmopolitan.**

**Local path:** `vendors/cosmo-include/`

---

### cosmo-cross-sdk
**Cross-compilation SDK.**

**Local path:** `vendors/cosmo-cross-sdk/`

---

### cosmo-teditor
**Text editor for Cosmopolitan (reference implementation).**

**Local path:** `vendors/submodules/cosmo-teditor/`

---

### awesome-cosmo
**Curated list of Cosmopolitan resources.**

**Local path:** `vendors/awesome-cosmo/`

---

## Priority 3: External References

### nicbarker/clay
**UI layout engine (single-header, pure C, ~4k LOC).**

| File | Purpose |
|------|---------|
| clay.h | The entire library (zero dependencies) |
| examples/ | Usage patterns |
| renderers/ | Raylib, SDL, HTML renderers |

**Features:** Flex-box layout, text wrapping, scrolling, aspect ratio scaling.
**Memory:** Static arena-based, no malloc/free (~3.5MB for 8192 elements).

**URL:** https://github.com/nicbarker/clay

**Integration:** uigen already generates Nuklear UI code from `.ui` specs.
Clay support can follow same pattern: lexgen + Lemon → `CLAY()` calls.

The "Lemon bindings" pattern: lexgen + Lemon compose to parse ANY DSL → generate ANY target.

---

## Tool Compatibility Quick Reference

| Tool | Status | Notes |
|------|--------|-------|
| cosmocc | ✅ Required | GCC 14.1.0 + Clang 19 |
| TinyCC (libtcc) | ❌ BANNED | "Invalid relocation entry" |
| Binaryen | ✅ OK | Use ludoplex/binaryen |
| Clang (cosmocc) | ✅ OK | `-mclang` flag, 3x faster C++ |
| libclang (library) | ⚠️ Avoid | Programmatic AST has relocation issues |
| Lemon | ✅ OK | Parser generator (SQLite's) |
| re2c | ✅ OK | Lexer generator |

---

## Known Platform Issues (from GitHub)

Check [jart/cosmopolitan/issues](https://github.com/jart/cosmopolitan/issues) before debugging.

### macOS (XNU)
| Issue | Description |
|-------|-------------|
| wait() timeout | Calls timeout after 72 minutes when no timeout specified (#1475) |
| df command | Cannot read mounted file systems (#1356) |

### Windows
| Issue | Description |
|-------|-------------|
| BCryptPrimitives.dll | Initialization errors on some systems (#1425) |
| ioctl | Terminal settings malfunctioning (#1453) |
| bash path | "cannot execute: required file not found" in Win10 bash (#1406) |

### Linux
| Issue | Description |
|-------|-------------|
| cosmo_dlopen | Segfault when invoking foreign functions on x86_64 (#1478) |

### ARM
| Issue | Description |
|-------|-------------|
| getcpu syscall | Error with unix.pledge() in Redbean (#1418) |

### Cross-Platform
| Issue | Description |
|-------|-------------|
| sys.platform | Python returns 'linux' on all platforms (#1462) |
| -msysv mode | Not functioning in 4.0.2 (#1486) |

**Workaround strategy:** Check issues before assuming a bug is in your code.

---

## Reading Order for New Agents

1. **jart/cosmopolitan README** - Understand the philosophy
2. **tool/cosmocc/README.md** - Understand the toolchain
3. **ape/README.md** - Understand the binary format
4. **This project's .claude/CLAUDE.md** - Understand our conventions
5. **Relevant vendor repo README** - For specific features

**Time estimate:** ~30 minutes of reading before writing code.

**This is NOT optional.** Skipping this step leads to:
- Using incompatible tools (TinyCC, libclang)
- Wrong APE assumptions (ELF vs PE)
- Non-portable code
- Wasted iterations

---

*Part of BDE with Models. Last updated: 2026-03-01*
